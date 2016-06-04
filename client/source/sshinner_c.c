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
            p_opt->opt.daemon.dummy = 0; 
        }
        else
        {
            if (json_object_object_get_ex(p_class,"r_mach_uuid",&p_store_obj))
            {
                ptr = json_object_get_string(p_store_obj); 
                sd_id128_from_string(ptr, &p_opt->opt.usr.r_mach_uuid); 
            }

            if (json_object_object_get_ex(p_class,"portmaps",&p_store_obj))
            {
                size_t len = json_object_array_length(p_store_obj);
                size_t i = 0;
                int from, to;
                for (i=0; i<len; ++i)
                {
                    json_object* p_tmp = json_object_array_get_idx(p_store_obj, i);
                    p_opt->opt.usr.maps[i].usrport    = json_object_get_int(
                                             json_object_object_get(p_tmp, "usrport"));
                    p_opt->opt.usr.maps[i].daemonport = json_object_get_int(
                                             json_object_object_get(p_tmp, "daemonport"));
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
    if (p_opt->C_TYPE == C_DAEMON) 
    {
        st_d_print("DUMMY:%d", p_opt->opt.daemon.dummy); 
    }
    else
    {
        st_d_print("ACC_MACHID:%s", SD_ID128_CONST_STR(p_opt->opt.usr.r_mach_uuid));
        int i = 0;
        for (i=0; i<10; i++)
        {
            if (p_opt->opt.usr.maps[i].from) 
            {
                st_d_print("FROM:%d, TO:%d", p_opt->opt.usr.maps[i].from,
                           p_opt->opt.usr.maps[i].to);
            }
            else
                break;
        }
    }

    return;
}

RET_T clt_daemon_send_data(P_CLT_OPT p_opt, void* data, int len)
{

    return RET_YES;
}

RET_T clt_usr_send_data(P_CLT_OPT p_opt, void* data, int len)
{

    return RET_YES;
}

/**
 * 客户端和远程服务器的交互
 */
void srv_bufferread_cb(struct bufferevent *bev, void *ptr)
{
    char buf[1024];
    int n;
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    while ((n = evbuffer_remove(input, buf, sizeof(buf))) > 0) 
    {
        fwrite("BUFFERREAD_CB:", 1, strlen("BUFFERREAD_CB:"), stderr);
        fwrite(buf, 1, n, stderr);
    }

    fprintf(stderr, "READ DONE!\n");
    //bufferevent_write(bev, msg, strlen(msg));
    return;
}


/**
 * 客户端处理本地USR/DAEMON的请求
 */

void bufferevent_cb(struct bufferevent *bev, short events, void *ptr)
{
    struct event_base *base = bufferevent_get_base(bev);
    int loop_terminate_flag = 0;

    //只有使用bufferevent_socket_connect进行的连接才会得到CONNECTED的事件
    if (events & BEV_EVENT_CONNECTED) 
    {
        st_d_print("GOT BEV_EVENT_CONNECTED event! ");
    } 
    else if (events & BEV_EVENT_ERROR) 
    {
        st_d_print("GOT BEV_EVENT_ERROR event! ");
        loop_terminate_flag = 1;
    } 
    else if (events & BEV_EVENT_EOF) 
    {
        st_d_print("GOT BEV_EVENT_EOF event! ");
        bufferevent_free(bev);
    }
    else if (events & BEV_EVENT_TIMEOUT) 
    {
        st_d_print("GOT BEV_EVENT_TIMEOUT event! ");
    } 
    else if (events & BEV_EVENT_READING) 
    {
        st_d_print("GOT BEV_EVENT_READING event! ");
    } 
    else if (events & BEV_EVENT_WRITING) 
    {
        st_d_print("GOT BEV_EVENT_WRITING event! ");
    }

    if (loop_terminate_flag)
    {
        bufferevent_free(bev);
        event_base_loopexit(base, NULL);
    }

    return;
}

void bufferread_cb(struct bufferevent *bev, void *ptr)
{
    char *msg = "SERVER MESSAGE: WOSHINICOL 桃子大人";
    char buf[1024];
    int n;
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    while ((n = evbuffer_remove(input, buf, sizeof(buf))) > 0) 
    {
        fwrite("BUFFERREAD_CB:", 1, strlen("BUFFERREAD_CB:"), stderr);
        fwrite(buf, 1, n, stderr);
    }

    fprintf(stderr, "READ DONE!\n");
    //bufferevent_write(bev, msg, strlen(msg));
    evbuffer_add(output, msg, strlen(msg));

    return;
}

void accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{ 
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

    getnameinfo (address, socklen,
               hbuf, sizeof(hbuf),sbuf, sizeof(sbuf),
               NI_NUMERICHOST | NI_NUMERICSERV);

    st_print("Welcome new connect (host=%s, port=%s)\n", hbuf, sbuf);

    /* We got a new connection! Set up a bufferevent for it. */
    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = 
        bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);

    /**
     * 对于服务端，一般都是阻塞在读，而如果要写，一般在read_cb中写回就可以了
     */
    bufferevent_setcb(bev, bufferread_cb, NULL, bufferevent_cb, NULL);
    bufferevent_enable(bev, EV_READ|EV_WRITE);

    st_d_print("Allocate and attach new bufferevent for new connectino...");

     return;
}

void accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();

    st_d_error( "Got an error %d (%s) on the listener. "
                "Shutting down...\n", err, evutil_socket_error_to_string(err));
    event_base_loopexit(base, NULL);

    return;
}


