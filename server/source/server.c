#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <pthread.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/buffer.h>

#include "st_others.h"
#include "sshinner_s.h"
#include "rbtree.h"

/**
 * This program aim on the server side of libevent
 */


SRV_OPT srvopt;

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

    memset(&srvopt, 0, sizeof(SRV_OPT));

    if(load_settings_server(&srvopt) == RET_NO)
    {
        st_d_error("加载settings.js配置文件出错！");
        exit(EXIT_FAILURE);
    }

    dump_srv_opts(&srvopt);

    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();
    SSL_library_init();     //SSL_library_init() always returns "1"

    /*加载SSL私钥*/
    FILE *fp = fopen(PRIVATE_KEY_FILE, "r"); 
    if (!fp)
    {
        st_d_error("SERVER读取私钥文件%s失败！", PRIVATE_KEY_FILE);
        exit(EXIT_FAILURE);
    }

    srvopt.uuid_tree = RB_ROOT;

    srvopt.p_prikey = RSA_new(); 

    if(PEM_read_RSAPrivateKey(fp, &srvopt.p_prikey, 0, 0) == NULL)
    {
        st_d_error("SERVER加载私钥失败！");
        fclose(fp);
        RSA_free(srvopt.p_prikey); 
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    srvopt.main_thread_id = pthread_self(); 
    srvopt.thread_objs = (P_THREAD_OBJ)calloc(sizeof(THREAD_OBJ), srvopt.thread_num);
    if (!srvopt.thread_objs) 
    {
        SYS_ABORT("申请THREAD_OBJ出错");
    }

    ss_create_worker_threads(srvopt.thread_num, srvopt.thread_objs);

    /*带配置产生event_base对象*/
    struct event_config *cfg;
    cfg = event_config_new();
    event_config_avoid_method(cfg, "select");          //避免使用select
    event_config_require_features(cfg, EV_FEATURE_ET);  //使用边沿触发类型
    srvopt.main_base = event_base_new_with_config(cfg);
    event_config_free(cfg);

    srvopt.evdns_base = evdns_base_new(srvopt.main_base, 1); 
    evdns_base_nameserver_ip_add(srvopt.evdns_base, "127.0.0.1");
    evdns_base_nameserver_ip_add(srvopt.evdns_base, "8.8.8.8");

    st_d_print("当前复用Event模式: %s", event_base_get_method(srvopt.main_base)); // epoll


    /**
     * 建立Listen侦听套接字
     */
    struct evconnlistener *listener;
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0);
    sin.sin_port = htons(srvopt.port); /* Port Num */

    listener = evconnlistener_new_bind(srvopt.main_base, accept_conn_cb, NULL,
            LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1/*backlog*/,
            (struct sockaddr*)&sin, sizeof(sin));

    if (!listener) 
    {
        st_d_error("创建侦听套接字出错！");
        return -1;
    }
    evconnlistener_set_error_cb(listener, accept_error_cb);
   
    /**
     * Main Loop Here
     */
    event_base_loop(srvopt.main_base, 0);

    if (srvopt.p_prikey) 
        RSA_free(srvopt.p_prikey);

    evconnlistener_free(listener); 

    evdns_base_free(srvopt.evdns_base, 0); 
    event_base_free(srvopt.main_base);

    st_d_print("Program terminated!");
    return 0;
}

