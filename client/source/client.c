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

#if 1
    // For debug with segment fault
    struct sigaction sa;
    sa.sa_handler = backtrace_info;
    sigaction(SIGSEGV, &sa, NULL);

    // ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGABRT, SIG_IGN);

#endif

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
        st_d_error("加载配置文件settings.json出错！");
        exit(EXIT_FAILURE);
    }

    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();
    SSL_library_init();     //SSL_library_init() always returns "1"

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
    st_d_print("当前复用Event模式: %s", event_base_get_method(base)); // epoll

    /*连接服务器*/
    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    unsigned int optval = 1;
    setsockopt(srv_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));//禁用NAGLE算法

    if(sc_connect_srv(srv_fd) != RET_YES) 
    {
        SYS_ABORT("连接服务器失败！");
    }

    if(cltopt.C_TYPE == C_DAEMON) 
    {
        if (sc_daemon_init_srv(srv_fd) != RET_YES) 
            SYS_ABORT("(Daemon)　服务器返回错误！");
    }
    else
    {
        if (sc_usr_init_srv(srv_fd) != RET_YES) 
            SYS_ABORT("(Usr)　服务器返回错误！");
    }

    st_d_print("客户端连接服务器OK！");

    /**
     * USR 建立本地Listen侦听套接字
     */

    if (cltopt.C_TYPE == C_USR)
    {
        int i = 0;
        for (i=0; i<MAX_PORT_NUM; i++)
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
                    st_d_error("[USR]创建侦听套接字失败 %d:%d", 
                               cltopt.maps[i].usrport, cltopt.maps[i].daemonport); 
                    continue;
                }
                evconnlistener_set_error_cb(listener, accept_error_cb);

                st_d_print("[USR]创建侦听套接字 %d:%d OK", 
                               cltopt.maps[i].usrport, cltopt.maps[i].daemonport); 
            }
            else
                break;
        }
    }
    
    encrypt_init(SD_ID128_CONST_STR(cltopt.mach_uuid), cltopt.enc_key);

    if (cltopt.C_TYPE == C_DAEMON && cltopt.ss5_port ) 
    {
        /**
         * 目前只考虑将sockets5代理使用线程池来处理，其它的端口暴露 
         * 基本都是长连接，不单独处理 
         */
        cltopt.thread_num = 5;

        cltopt.main_thread_id = pthread_self(); 
        cltopt.thread_objs = (P_THREAD_OBJ)calloc(sizeof(THREAD_OBJ), cltopt.thread_num);
        if (!cltopt.thread_objs) 
        {
            SYS_ABORT("申请THREAD_OBJ出错");
        }


        sc_create_ss5_worker_threads(cltopt.thread_num, cltopt.thread_objs); 

        st_d_print("[DAEMON]创建sockets5代理端口：%d", cltopt.ss5_port); 

        struct evconnlistener *listener;
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(0);
        sin.sin_port = htons(cltopt.ss5_port); /* Port Num */

        listener = evconnlistener_new_bind(base, ss5_accept_conn_cb, NULL,
                LEV_OPT_LEAVE_SOCKETS_BLOCKING/* 阻塞 */|LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, 
                -1/*backlog 连接无限制*/,
                (struct sockaddr*)&sin, sizeof(sin));

        if (!listener) 
        {
            st_d_error("[DAEMON]sockets5代理创建侦听套接字失败 %d", cltopt.ss5_port); 
            exit(EXIT_FAILURE); 
        }
        evconnlistener_set_error_cb(listener, accept_error_cb);

        st_d_print("[DAEMON]sockets5代理创建侦听套接字OK %d", cltopt.ss5_port); 

    }



    if (cltopt.C_TYPE == C_DAEMON && cltopt.dns_port) 
    {
        st_d_print("[DAEMON]创建DNS代理端口：%d", cltopt.dns_port); 
        if (cltopt.dns_port != 53) 
        {
            st_d_print("[DAEMON]请注意标准DNS侦听#53端口！");
        }

        int dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (dns_socket < 0 )
        {
            st_d_error("Create DNS socket error!");
            exit(EXIT_FAILURE);
        }

        unsigned int optval = 1;
        setsockopt(dns_socket, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));//禁用NAGLE算法
        setsockopt(dns_socket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
        evutil_make_socket_closeonexec(dns_socket);
        evutil_make_socket_nonblocking(dns_socket);

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(0);
        sin.sin_port = htons(cltopt.dns_port); /* Port Num */

        if (bind(dns_socket, (struct sockaddr *)&sin, sizeof(sin)))
        {
            st_d_error("Bind DNS socket error!");
            exit(EXIT_FAILURE);
        }

        cltopt.dns_transid_port_map = (unsigned short*)malloc(sizeof(unsigned short) * 0xFFFF);
        if (!cltopt.dns_transid_port_map) 
        {
            st_d_error("Malloc for requestid-port failed!");
            exit(EXIT_FAILURE);
        }

        P_PORTTRANS p_trans = sc_create_trans(cltopt.dns_port); 
        if (!p_trans)
        {
            st_d_error("本地无空闲TRANS!");
            exit(EXIT_FAILURE);
        }
        p_trans->is_enc = 1;
        p_trans->l_port = cltopt.dns_port;
        encrypt_ctx_init(&p_trans->ctx_enc, p_trans->l_port, cltopt.enc_key, 1); 
        encrypt_ctx_init(&p_trans->ctx_dec, p_trans->l_port, cltopt.enc_key, 0);
        // 建立DNS UDP事件侦听
        p_trans->extra_ev = event_new(base, dns_socket, EV_READ | EV_PERSIST, 
                                      dns_client_to_proxy_cb, p_trans);


        int dns_srv_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(sc_connect_srv(dns_srv_fd) != RET_YES) 
        {
            SYS_ABORT("连接服务器失败！");
        }

        sc_daemon_dns_init_srv(dns_srv_fd, p_trans->l_port, 12333);
        evutil_make_socket_nonblocking(dns_srv_fd);

        // later enabled
        //event_add(p_trans->extra_ev, NULL) != 0);

        p_trans->srv_bev = bufferevent_socket_new(base, dns_srv_fd, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(p_trans->srv_bev, dns_bufferread_cb_enc, NULL, dns_bufferevent_cb, p_trans);

        st_d_print("[DAEMON]DNS代理创建侦听套接字OK %d", cltopt.dns_port); 
    }

    sc_set_eventcb_srv(srv_fd, base); 

    /**
     * Main Loop Here
     */

    event_base_loop(base, 0);
    event_base_free(base);
    st_d_print("程序退出！！！！");
    return 0;
}


