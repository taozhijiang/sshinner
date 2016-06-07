#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 


#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "sshinner_c.h"

static RET_T json_fetch_and_copy(struct json_object *p_obj, const char* key, char* store, int max_len)
{
    json_object *p_store_obj = NULL;

    if (!p_obj || !key || !strlen(key) || !store)
        return RET_NO;

    if (json_object_object_get_ex(p_obj, key, &p_store_obj) &&
        json_object_get_string_len(p_store_obj))
    {
        strncpy(store, json_object_get_string(p_store_obj), max_len);
        return RET_YES;
    }

    return RET_NO;
}

extern RET_T load_settings_client(P_CLT_OPT p_opt)
{
    json_object *p_obj = NULL;
    json_object *p_class = NULL;
    json_object *p_store_obj = NULL;
    char *ptr = NULL;

    if (!p_opt)
        return RET_NO;

    if( ! (p_obj = json_object_from_file("settings.json")) )
        return RET_NO;

    if(json_object_object_get_ex(p_obj,"server",&p_class))
    {
        st_d_print("handling server configuration....");

        if (json_object_object_get_ex(p_class,"ipaddr",&p_store_obj))
        {
            ptr = json_object_get_string(p_store_obj);
            p_opt->srv.sin_family = AF_INET;
            p_opt->srv.sin_addr.s_addr = inet_addr(ptr);
        }

        if (json_object_object_get_ex(p_class,"port",&p_store_obj))
            p_opt->srv.sin_port = htons(json_object_get_int(p_store_obj)); 
    }

    if(json_object_object_get_ex(p_obj,"client",&p_class))
    {
        st_d_print("handling client configuration....");

        json_fetch_and_copy(p_class, "username", p_opt->username, 128); 

        if (json_object_object_get_ex(p_class,"userid",&p_store_obj))
            p_opt->userid = json_object_get_int64(p_store_obj); 


        if (p_opt->C_TYPE == C_DAEMON) 
        {
            p_opt->session_uuid = p_opt->mach_uuid;
        }
        else
        {
            if (json_object_object_get_ex(p_class,"r_mach_uuid",&p_store_obj))
            {
                ptr = json_object_get_string(p_store_obj); 
                sd_id128_from_string(ptr, &p_opt->session_uuid); 
            }

            if (json_object_object_get_ex(p_class,"portmaps",&p_store_obj))
            {
                size_t len = json_object_array_length(p_store_obj);
                size_t i = 0;
                int from, to;
                for (i=0; i<len; ++i)
                {
                    json_object* p_tmp = json_object_array_get_idx(p_store_obj, i);
                    p_opt->maps[i].usrport    = json_object_get_int(
                                             json_object_object_get(p_tmp, "usrport"));
                    p_opt->maps[i].daemonport = json_object_get_int(
                                             json_object_object_get(p_tmp, "daemonport"));
                    p_opt->maps[i].bev = NULL;
                }
            }
        }

    }

    json_object_put(p_obj);
    return RET_YES;
}

extern void dump_clt_opts(P_CLT_OPT p_opt)
{
    if (!p_opt)
        return;

    st_d_print("WORK MODE: %s", p_opt->C_TYPE==C_DAEMON ?
               "C_DAEMON":"C_USR");

    st_d_print("");
    st_d_print("USRNAME:%s", p_opt->username); 
    st_d_print("USRID:%lu", p_opt->userid);
    st_d_print("HOSTNAME:%s", p_opt->hostname);
    st_d_print("HOSTMACHID:%s", SD_ID128_CONST_STR(p_opt->mach_uuid));
    
    st_d_print("");
    st_d_print("SERVERIP:%s", inet_ntoa(p_opt->srv.sin_addr));
    st_d_print("SERVERPORT:%d", ntohs(p_opt->srv.sin_port));

    st_d_print("");
    st_d_print("SESSION_UUID: %s", SD_ID128_CONST_STR(p_opt->session_uuid));
    if (p_opt->C_TYPE == C_USR) 
    {
        int i = 0;
        for (i = 0; i < MAX_PORTMAP_NUM; i++) 
        {
            if (p_opt->maps[i].usrport) 
            {
                st_d_print("FROM:%d, TO:%d", p_opt->maps[i].usrport, 
                           p_opt->maps[i].daemonport); 
            }
            else
                break;
        }
    }

    return;
}


// used from USR SIDE
P_PORTMAP sc_find_portmap(unsigned short usrport)
{
    P_PORTMAP p_map = NULL;
    int i = 0;

    for (i = 0; i < MAX_PORTMAP_NUM; i++) 
    {
        if (cltopt.maps[i].usrport == usrport) 
        {
            p_map = &cltopt.maps[i];
        }
    }

    return p_map;
}

// used from DAEMON SIDE
P_PORTMAP sc_find_create_portmap(unsigned short daemonport)
{
    P_PORTMAP p_map = NULL;
    int i = 0;

    for (i = 0; i < MAX_PORTMAP_NUM; i++) 
    {
        if (cltopt.maps[i].daemonport == daemonport) 
        {
            p_map = &cltopt.maps[i];
        }
    }

    for (i = 0; i < MAX_PORTMAP_NUM; i++) 
    {
        if (cltopt.maps[i].daemonport == 0) 
        {
            p_map = &cltopt.maps[i];
            p_map->daemonport = daemonport;
            p_map->bev = NULL;
        }
    }

    return p_map;
}

RET_T sc_daemon_connect_srv(int srv_fd)
{
    char buff[4096];
    P_PKG_HEAD p_head = NULL;

    memset(buff, 0, sizeof(buff));
    p_head = GET_PKG_HEAD(buff);

    p_head->type = 'C';
    p_head->direct = DAEMON_USR; 
    p_head->mach_uuid = cltopt.mach_uuid;
    
    /*发送DAEMON的配置信息*/
    json_object* ajgResponse =  json_object_new_object(); 
    json_object_object_add (ajgResponse, "hostname", 
                           json_object_new_string(cltopt.hostname));
    json_object_object_add (ajgResponse, "username", 
                           json_object_new_string(cltopt.username));
    json_object_object_add (ajgResponse, "userid", 
                           json_object_new_int64(cltopt.userid)); 

    const char* ret_str = json_object_to_json_string (ajgResponse);
    p_head->dat_len = strlen(ret_str) + 1;
    p_head->crc = crc32(0L, ret_str, strlen(ret_str) + 1);

    strcpy(GET_PKG_BODY(buff), ret_str);
    write(srv_fd, buff, HEAD_LEN + p_head->dat_len);

    json_object_put(ajgResponse);

    PKG_HEAD ret_head;
    read(srv_fd, &ret_head, HEAD_LEN); 
    if (ret_head.type == 'C' && ret_head.direct == USR_DAEMON && ret_head.ext == 'O') 
    {
        return RET_YES;
    }
    else
    {
        return RET_NO;
    }

}

RET_T sc_usr_connect_srv(int srv_fd)
{
    char buff[4096];
    P_PKG_HEAD p_head = NULL;

    memset(buff, 0, sizeof(buff));
    p_head = GET_PKG_HEAD(buff);

    p_head->type = 'C';
    p_head->direct = USR_DAEMON; 
    p_head->mach_uuid = cltopt.mach_uuid;
    
    /*发送DAEMON的配置信息*/
    json_object* ajgResponse =  json_object_new_object(); 
    json_object_object_add (ajgResponse, "hostname", 
                           json_object_new_string(cltopt.hostname));
    json_object_object_add (ajgResponse, "username", 
                           json_object_new_string(cltopt.username));
    json_object_object_add (ajgResponse, "userid", 
                           json_object_new_int64(cltopt.userid)); 
    json_object_object_add (ajgResponse, "r_mach_uuid", 
                           json_object_new_string(SD_ID128_CONST_STR(cltopt.session_uuid))); 

    const char* ret_str = json_object_to_json_string (ajgResponse);
    p_head->dat_len = strlen(ret_str) + 1;
    p_head->crc = crc32(0L, ret_str, strlen(ret_str) + 1);

    strcpy(GET_PKG_BODY(buff), ret_str);
    write(srv_fd, buff, HEAD_LEN + p_head->dat_len);

    json_object_put(ajgResponse);

    PKG_HEAD ret_head;
    read(srv_fd, &ret_head, HEAD_LEN);
    if (ret_head.type == 'C' && ret_head.direct == DAEMON_USR && ret_head.ext == 'O') 
    {
        return RET_YES;
    }
    else
    {
        return RET_NO;
    }

}


RET_T sc_connect_srv(int srv_fd)
{
    int reuseaddr_on = 1;

    if (setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, 
		sizeof(reuseaddr_on)) == -1)
    {
        st_d_print("Reuse socket opt faile!\n");
        return RET_NO;
    }
    if (connect(srv_fd, (struct sockaddr *)&cltopt.srv, sizeof(cltopt.srv))) 
    {
        st_d_error("Connect to server failed!\n");
        return RET_NO;
    }
    
    return RET_YES;
}

void sc_set_eventcb_srv(int srv_fd, struct event_base *base)
{
    struct bufferevent *srv_bev = NULL;

    evutil_make_socket_nonblocking(srv_fd);
    srv_bev = bufferevent_socket_new(base, srv_fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(srv_bev, srv_bufferread_cb, NULL, srv_bufferevent_cb, NULL);
    cltopt.srv_bev = srv_bev;
    bufferevent_enable(srv_bev, EV_READ|EV_WRITE);

    return;
}
