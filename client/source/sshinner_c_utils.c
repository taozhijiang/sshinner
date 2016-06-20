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

    slist_init(&p_opt->trans);

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

        if (json_object_object_get_ex(p_class,"ss5_port",&p_store_obj))
            p_opt->ss5_port = json_object_get_int64(p_store_obj); 
        else
            p_opt->ss5_port = 0;

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
                if (len > MAX_PORT_NUM)
                {
                    st_d_error("只支持%d/%d", MAX_PORT_NUM, len); 
                    len = MAX_PORT_NUM;
                }
                size_t i = 0;
                int from, to;
                for (i=0; i<len; ++i)
                {
                    json_object* p_tmp = json_object_array_get_idx(p_store_obj, i);
                    p_opt->maps[i].usrport    = json_object_get_int(
                                             json_object_object_get(p_tmp, "usrport"));
                    p_opt->maps[i].daemonport = json_object_get_int(
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
    st_d_print("SESSION_UUID: %s", SD_ID128_CONST_STR(p_opt->session_uuid));

    st_d_print("");
    st_d_print("SOCKET5 PROXY: %d", p_opt->ss5_port); 

    if (p_opt->C_TYPE == C_USR) 
    {
        int i = 0;
        for (i = 0; i < MAX_PORT_NUM; i++) 
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


// used from DAEMON SIDE
P_PORTMAP sc_find_daemon_portmap(unsigned short daemonport, int createit)
{
    P_PORTMAP p_map = NULL;
    int i = 0;

    for (i = 0; i < MAX_PORT_NUM; i++) 
    {
        if (cltopt.maps[i].daemonport == daemonport) 
        {
            p_map = &cltopt.maps[i];
            return p_map;
        }
    }

    if (! createit)
        return NULL;

    for (i = 0; i < MAX_PORT_NUM; i++)
    {
        if (cltopt.maps[i].daemonport == 0) 
        {
            p_map = &cltopt.maps[i];
            p_map->daemonport = daemonport;
            break;
        }
    }

    return p_map;
}

extern P_PORTTRANS sc_find_trans(unsigned short l_sock)
{
    P_PORTTRANS p_trans = NULL;

    if (slist_empty(&cltopt.trans))
        return NULL;

    slist_for_each_entry(p_trans, &cltopt.trans, list)
    {
        if (p_trans->l_port == l_sock) 
        {
           return p_trans;
        }
    }

    return NULL;
}

extern P_PORTTRANS sc_create_trans(unsigned short l_sock)
{
    P_PORTTRANS p_trans = NULL;

    if (sc_find_trans(l_sock))
    {
        st_d_error("TRANS已经存在：%d", l_sock);
        return NULL;
    }

    p_trans = (P_PORTTRANS)calloc(sizeof(PORTTRANS), 1);
    if (!p_trans)
    {
        st_d_error("TRANS申请内存失败！");
        return NULL;
    }

    p_trans->l_port = l_sock;
    p_trans->local_bev = NULL;
    p_trans->srv_bev = NULL;
    slist_add(&p_trans->list, &cltopt.trans); 

    return p_trans;
}


extern RET_T sc_free_trans(P_PORTTRANS p_trans)
{
    if (!p_trans || !p_trans->l_port) 
    {
        st_d_error("Free参数失败！");
        return RET_NO;
    }

    if (p_trans->srv_bev)
        bufferevent_free(p_trans->srv_bev);
    if (p_trans->local_bev) 
        bufferevent_free(p_trans->local_bev);

    if (p_trans->is_enc) 
    {
        encrypt_ctx_free(&p_trans->ctx_enc);
        encrypt_ctx_free(&p_trans->ctx_enc);
    }

    slist_remove(&p_trans->list, &cltopt.trans); 
    free(p_trans);

    return RET_YES;
}

extern RET_T sc_free_all_trans(void)
{
    P_PORTTRANS p_trans = NULL;
    P_SLIST_HEAD pos = NULL, n = NULL; 

    if (slist_empty(&cltopt.trans))
    {
        return RET_YES;
    }

    slist_for_each_safe(pos, n, &cltopt.trans)
    {
        p_trans = list_entry(pos, PORTTRANS, list); 
        st_d_print("释放：%d", p_trans->l_port);

        if (p_trans->srv_bev)
            bufferevent_free(p_trans->srv_bev);
        if (p_trans->local_bev) 
            bufferevent_free(p_trans->local_bev);
        slist_remove(&p_trans->list, &cltopt.trans); 
        free(p_trans);
    }

    return RET_YES;
}

RET_T sc_daemon_init_srv(int srv_fd)
{
    char buff[4096];
    P_CTL_HEAD p_head = NULL;
    RSA* p_rsa = NULL;

    /*加载SSL公钥*/
    FILE* fp = fopen(PUBLIC_KEY_FILE, "r");
    if (!fp)
    {
        st_d_error("CLIENT读取公钥文件%s失败！", PUBLIC_KEY_FILE);
        exit(EXIT_FAILURE);
    }
    p_rsa = RSA_new();

    if(PEM_read_RSA_PUBKEY(fp, &p_rsa, 0, 0) == NULL)
    {
        st_d_error("CLIENT USR加载公钥失败！");
        fclose(fp);
        RSA_free(p_rsa); 
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    memset(buff, 0, sizeof(buff));
    p_head = GET_CTL_HEAD(buff);

    p_head->direct = DAEMON_USR; 
    p_head->cmd = HD_CMD_INIT;
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

    if (strlen(ret_str) + 1 > (RSA_size(p_rsa) - 11 ) )
    {
        st_d_error("消息体太长：%d", strlen(ret_str)+1 );
        json_object_put(ajgResponse);
        RSA_free(p_rsa);
        return RET_NO;
    }

    int len = RSA_public_encrypt(strlen(ret_str)+1, ret_str, GET_CTL_BODY(buff),
                       p_rsa, RSA_PKCS1_PADDING);

    if (len < 0 ) 
    {
        st_d_error("公钥加密失败：%d", len);
        ERR_print_errors_fp(stderr);
        json_object_put(ajgResponse);
        RSA_free(p_rsa);
        return RET_NO;
    }

    RSA_free(p_rsa);

    p_head->dat_len = len;
    p_head->crc = crc32(0L, GET_CTL_BODY(buff), len);

    write(srv_fd, buff, CTL_HEAD_LEN + p_head->dat_len);

    json_object_put(ajgResponse);

    CTL_HEAD ret_head;
    read(srv_fd, &ret_head, CTL_HEAD_LEN); 
    if (ret_head.direct == USR_DAEMON && ret_head.cmd == HD_CMD_OK) 
    {
        return RET_YES;
    }
    else
    {
        return RET_NO;
    }

}

RET_T sc_usr_init_srv(int srv_fd)
{
    char buff[4096];
    P_CTL_HEAD p_head = NULL;
    RSA* p_rsa = NULL;

    /*加载SSL公钥*/
    FILE* fp = fopen(PUBLIC_KEY_FILE, "r");
    if (!fp)
    {
        st_d_error("CLIENT读取公钥文件%s失败！", PUBLIC_KEY_FILE);
        exit(EXIT_FAILURE);
    }
    p_rsa = RSA_new();

    if(PEM_read_RSA_PUBKEY(fp, &p_rsa, 0, 0) == NULL)
    {
        st_d_error("CLIENT USR加载公钥失败！");
        fclose(fp);
        RSA_free(p_rsa); 
        return RET_NO;
    }

    fclose(fp);

    memset(buff, 0, sizeof(buff));
    p_head = GET_CTL_HEAD(buff);

    p_head->direct = USR_DAEMON;
    p_head->cmd = HD_CMD_INIT; 
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

    if (strlen(ret_str) + 1 > (RSA_size(p_rsa) - 11 ) )
    {
        st_d_error("消息体太长：%d", strlen(ret_str)+1 );
        ERR_print_errors_fp(stderr);
        json_object_put(ajgResponse);
        RSA_free(p_rsa); 
        return RET_NO;
    }

    int len = RSA_public_encrypt(strlen(ret_str)+1, ret_str, GET_CTL_BODY(buff),
                       p_rsa, RSA_PKCS1_PADDING);

    RSA_free(p_rsa); 

    if (len < 0 ) 
    {
        st_d_error("公钥加密失败：%d", len);
        json_object_put(ajgResponse);
        return RET_NO;
    }

    p_head->dat_len = len;
    p_head->crc = crc32(0L, GET_CTL_BODY(buff), len);

    write(srv_fd, buff, CTL_HEAD_LEN + p_head->dat_len);

    json_object_put(ajgResponse);

    CTL_HEAD ret_head;
    read(srv_fd, &ret_head, CTL_HEAD_LEN);
    if (ret_head.direct == DAEMON_USR && ret_head.cmd == HD_CMD_OK) 
    {
        return RET_YES;
    }
    else
    {
        return RET_NO;
    }

}

RET_T sc_daemon_ss5_init_srv(int srv_fd, const char* request, unsigned short l_port)
{
    char buff[4096];
    P_CTL_HEAD p_head = NULL;
    RSA* p_rsa = NULL;

    /*加载SSL公钥*/
    FILE* fp = fopen(PUBLIC_KEY_FILE, "r");
    if (!fp)
    {
        st_d_error("CLIENT读取公钥文件%s失败！", PUBLIC_KEY_FILE);
        exit(EXIT_FAILURE);
    }
    p_rsa = RSA_new();

    if(PEM_read_RSA_PUBKEY(fp, &p_rsa, 0, 0) == NULL)
    {
        st_d_error("CLIENT USR加载公钥失败！");
        fclose(fp);
        RSA_free(p_rsa); 
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    memset(buff, 0, sizeof(buff));
    p_head = GET_CTL_HEAD(buff);

    p_head->direct = DAEMON_USR; 
    p_head->cmd = HD_CMD_SS5; 
    p_head->extra_param = l_port;
    p_head->mach_uuid = cltopt.mach_uuid;

    int dat_len = 0;
    if (request[3] == 0x01)
    {
        dat_len = 4 + 4 + 2;
    }
    else
    {
        dat_len = 4 + 1 + request[4] + 2;
    }

    if (dat_len + 1 > (RSA_size(p_rsa) - 11 ) )
    {
        st_d_error("消息体太长!" );
        RSA_free(p_rsa);
        return RET_NO;
    }

    int len = RSA_public_encrypt(dat_len, request, GET_CTL_BODY(buff),
                       p_rsa, RSA_PKCS1_PADDING);

    if (len < 0 ) 
    {
        st_d_error("公钥加密失败：%d", len);
        ERR_print_errors_fp(stderr);
        RSA_free(p_rsa);
        return RET_NO;
    }

    RSA_free(p_rsa);

    p_head->dat_len = len;
    p_head->crc = crc32(0L, GET_CTL_BODY(buff), len);

    write(srv_fd, buff, CTL_HEAD_LEN + p_head->dat_len);

    return RET_YES;
}


/**
 * 没有消息负载的发送
 */
RET_T sc_send_head_cmd(int cmd, unsigned long extra_param, 
                        unsigned short usrport, unsigned daemonport)
{
    CTL_HEAD head;
    memset(&head, 0, CTL_HEAD_LEN);

    if (cltopt.srv_bev == NULL) 
    {
        st_d_error("cltopt.srv_bev == NULL");
        return RET_NO;
    }

    head.direct = (cltopt.C_TYPE == C_USR ? USR_DAEMON : DAEMON_USR); 
    head.cmd = cmd;
    head.extra_param = extra_param;
    head.mach_uuid = cltopt.session_uuid;
    head.usrport = usrport;
    head.daemonport = daemonport;

    bufferevent_write(cltopt.srv_bev, &head, CTL_HEAD_LEN);

    return RET_YES;
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

