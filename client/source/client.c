#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include <sys/socket.h>

#include <event2/listener.h>
#include <event2/util.h>
#include <event2/buffer.h>

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "st_others.h"
#include "sshinner_c.h"


static void usage(void)
{
    fprintf(stderr, "  *******************************************************************\n" );
    fprintf(stderr, "    USAGE:                                                           \n");
    fprintf(stderr, "    client [-D] [-h]                                                 \n");
    fprintf(stderr, "    -D    默认USER模式启动，会读取settings.json配置文件，该参数用DEMO模式启动 \n");
    fprintf(stderr, "    -h    帮助                                                        \n");
    fprintf(stderr, "  *******************************************************************\n");
}

int main(int argc, char* argv[])
{
    int opt_g = 0;
    struct event_base *base;
    CLT_OPT cltopt;
    memset(&cltopt, 0, sizeof(CLT_OPT));

    cltopt.C_TYPE = C_USR;
    while( (opt_g = getopt(argc, argv, "Dh")) != -1 )
    {
        switch(opt_g)
        {
            case 'D':
                cltopt.C_TYPE = C_DAEMON;
                break;
            case 'h':
            default:
                usage();
                exit(EXIT_SUCCESS);
        }
    }

    if(load_settings_client(&cltopt) == RET_NO)
    {
        st_d_error("Loading settings.json error!");
        exit(EXIT_FAILURE);
    }

    //int sd_id128_from_string(const char *s, sd_id128_t *ret);
    sd_id128_get_machine(&cltopt.mach_uuid);
    gethostname(cltopt.hostname, sizeof(cltopt.hostname)); 
    st_d_print("CURRENT MACH_ID:%s, HOSTNAME:%s", SD_ID128_CONST_STR(cltopt.mach_uuid), 
               cltopt.hostname);

    if (cltopt.C_TYPE == C_DAEMON) 
        st_d_print("PLEASE REMEMEBER SET MACH_ID FOR USER TYPE!");

    dump_clt_opts(&cltopt);


    /*带配置产生event_base对象*/
    struct event_config *cfg;
    cfg = event_config_new();
    event_config_avoid_method(cfg, "select");   //避免使用select
    event_config_require_features(cfg, EV_FEATURE_ET);  //使用边沿触发类型
    base = event_base_new_with_config(cfg);
    event_config_free(cfg);
    st_d_print("Current Using Method: %s", event_base_get_method(base)); // epoll

    /*连接服务器*/
    int srv_fd;
    srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuseaddr_on = 1;
    if (setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, 
		sizeof(reuseaddr_on)) == -1)
    {
        st_d_print("Reuse socket opt faile!\n");
        exit(EXIT_FAILURE);
    }
    if (connect(srv_fd, (struct sockaddr *)&cltopt.srv, sizeof(cltopt.srv))) 
    {
        st_d_error("Connect to server failed!\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        st_d_print("Connected to server OK!");
    }


    evutil_make_socket_nonblocking(srv_fd);
    struct bufferevent *bev = 
        bufferevent_socket_new(base, srv_fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(bev, srv_bufferread_cb, NULL, NULL, NULL);
    bufferevent_enable(bev, EV_READ|EV_WRITE);

    if (cltopt.C_TYPE == C_DAEMON) 
    {
        PKG_HEAD head;
        memset(&head, 0, HEAD_LEN);

        head.version = 1;
        head.type = 'C';
        head.direct = 2;
        memcpy(&head.mach_uuid, &cltopt.mach_uuid, sizeof(cltopt.mach_uuid));
        
        /*发送DAEMON的配置信息*/
        json_object* ajgResponse =  json_object_new_object(); 
        json_object_object_add (ajgResponse, "hostname", 
                               json_object_new_string(cltopt.hostname));
        json_object_object_add (ajgResponse, "username", 
                               json_object_new_string(cltopt.username));
        json_object_object_add (ajgResponse, "userid", 
                               json_object_new_int64(cltopt.userid)); 

        const char* ret_str = json_object_to_json_string (ajgResponse);
        head.dat_len = strlen(ret_str) + 1;
        head.crc = crc32(0L, ret_str, strlen(ret_str) + 1);

        bufferevent_write(bev, &head, HEAD_LEN);
        bufferevent_write(bev, ret_str, head.dat_len);

        json_object_put(ajgResponse);
    }
    else
    {
        
        PKG_HEAD head;
        memset(&head, 0, HEAD_LEN);
        head.version = 1;
        head.type = 'C';
        head.direct = 1;
        memcpy(&head.mach_uuid, &cltopt.mach_uuid, sizeof(head.mach_uuid));
        
        /*发送DAEMON的配置信息*/
        json_object* ajgResponse =  json_object_new_object(); 
        json_object_object_add (ajgResponse, "hostname", 
                               json_object_new_string(cltopt.hostname));
        json_object_object_add (ajgResponse, "username", 
                               json_object_new_string(cltopt.username));
        json_object_object_add (ajgResponse, "userid", 
                               json_object_new_int64(cltopt.userid)); 
        json_object_object_add (ajgResponse, "r_mach_uuid", 
                               json_object_new_string(SD_ID128_CONST_STR(cltopt.opt.usr.r_mach_uuid))); 

        const char* ret_str = json_object_to_json_string (ajgResponse);
        head.dat_len = strlen(ret_str) + 1;
        head.crc = crc32(0L, ret_str, strlen(ret_str) + 1);

        bufferevent_write(bev, &head, HEAD_LEN);
        bufferevent_write(bev, ret_str, head.dat_len);

        json_object_put(ajgResponse);
    }


    /**
     * 建立本地Listen侦听套接字
     */

    int i = 0;
    for (i=0; i<10; i++)
    {
        if (cltopt.opt.usr.maps[i].from) 
        {
            struct evconnlistener *listener;
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = htonl(0);
            sin.sin_port = htons(cltopt.opt.usr.maps[i].from); /* Port Num */

            listener = evconnlistener_new_bind(base, accept_conn_cb, &cltopt.opt.usr.maps[i],
                    LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1/*backlog 连接无限制*/,
                    (struct sockaddr*)&sin, sizeof(sin));

            if (!listener) 
            {
                    st_d_error("Couldn't create listener");
                    return -1;
            }
            evconnlistener_set_error_cb(listener, accept_error_cb);
            cltopt.opt.usr.maps[i].listener = listener;
        }
        else
            break;
    }

    

    /**
     * Main Loop Here
     */
    event_base_loop(base, 0);


    for (i=0; i<10; i++)
    {
        if (cltopt.opt.usr.maps[i].listener) 
            evconnlistener_free(cltopt.opt.usr.maps[i].listener);
    }

    
    event_base_free(base);
    st_d_print("Program terminated!");
    return 0;
}




