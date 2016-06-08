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

struct event_base *base;
CLT_OPT cltopt;

int main(int argc, char* argv[])
{
    int opt_g = 0;
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
    {
        cltopt.session_uuid = cltopt.mach_uuid;
        st_d_print("PLEASE REMEMEBER SET MACH_ID FOR USER TYPE!");
    }

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
    if (sc_connect_srv(srv_fd) != RET_YES)
    {
        SYS_ABORT("Connect to server failed!");
    }

    if (cltopt.C_TYPE == C_DAEMON) 
    {
        if(sc_daemon_connect_srv(srv_fd) != RET_YES)
            SYS_ABORT("(Daemon)Server Response failed!");
    }
    else
    {
        if(sc_usr_connect_srv(srv_fd) != RET_YES)
            SYS_ABORT("(Usr)Server Response failed!");
    }

    st_d_print("Client setup with server ok!");

    /**
     * USR 建立本地Listen侦听套接字
     */

    if (cltopt.C_TYPE == C_USR)
    {
        int i = 0;
        for (i=0; i<MAX_PORTMAP_NUM; i++)
        {
            if (cltopt.maps[i].usrport) 
            {
                struct evconnlistener *listener;
                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                sin.sin_family = AF_INET;
                sin.sin_addr.s_addr = htonl(0);
                sin.sin_port = htons(cltopt.maps[i].usrport); /* Port Num */

                listener = evconnlistener_new_bind(base, accept_conn_cb, &cltopt.maps[i],
                        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1/*backlog 连接无限制*/,
                        (struct sockaddr*)&sin, sizeof(sin));

                if (!listener) 
                {
                    st_d_error("Couldn't create listener for %d:%d", 
                               cltopt.maps[i].usrport, cltopt.maps[i].daemonport); 
                    continue;
                }
                evconnlistener_set_error_cb(listener, accept_error_cb);
                cltopt.maps[i].bev = NULL;

                st_d_print("Setup listener for %d:%d OK", 
                               cltopt.maps[i].usrport, cltopt.maps[i].daemonport); 
            }
            else
                break;
        }
    }
    

    sc_set_eventcb_srv(srv_fd, base);

    /**
     * Main Loop Here
     */
    event_base_loop(base, 0);
        
    event_base_free(base);
    st_d_print("Program terminated!");
    return 0;
}


