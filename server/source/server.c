#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>

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
struct  event_base *base;

int main(int argc, char* argv[])
{
    memset(&srvopt, 0, sizeof(SRV_OPT));

    if(load_settings_server(&srvopt) == RET_NO)
    {
        st_d_error("Loading settings.json error!");
        exit(EXIT_FAILURE);
    }

    dump_srv_opts(&srvopt);
    srvopt.uuid_tree = RB_ROOT;

    /*带配置产生event_base对象*/
    struct event_config *cfg;
    cfg = event_config_new();
    event_config_avoid_method(cfg, "select");   //避免使用select
    event_config_require_features(cfg, EV_FEATURE_ET);  //使用边沿触发类型
    base = event_base_new_with_config(cfg);
    event_config_free(cfg);

    st_d_print("Current Using Method: %s", event_base_get_method(base)); // epoll


    /**
     * 建立Listen侦听套接字
     */
    struct evconnlistener *listener;
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0);
    sin.sin_port = htons(srvopt.port); /* Port Num */

    listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
            LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1/*backlog*/,
            (struct sockaddr*)&sin, sizeof(sin));

    if (!listener) 
    {
            st_d_error("Couldn't create listener");
            return -1;
    }
    evconnlistener_set_error_cb(listener, accept_error_cb);
   
    /**
     * Main Loop Here
     */
    event_base_loop(base, 0);


    evconnlistener_free(listener);
    event_base_free(base);

    st_d_print("Program terminated!");
    return 0;
}

