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

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "st_others.h"
#include "sshinner_c.h"


/**
 * This program aim on the server side of libevent
 */

void bufferevent_cb(struct bufferevent *bev, short events, void *ptr);
void bufferread_cb(struct bufferevent *bev, void *ptr);

static void
accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx);

static void accept_error_cb(struct evconnlistener *listener, void *ctx);

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

    /**
     * 建立Listen侦听套接字
     */
    struct evconnlistener *listener;
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(0);
    sin.sin_port = htons(cltopt.l_port); /* Port Num */

    listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
            LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1/*backlog 连接无限制*/,
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

/**
 * 读取事件，主要进行数据转发 
 */
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

/**
 * 监听套接字响应事件
 */
static void
accept_conn_cb(struct evconnlistener *listener,
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

static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();

    st_d_error( "Got an error %d (%s) on the listener. "
            "Shutting down...\n", err, evutil_socket_error_to_string(err));
    event_base_loopexit(base, NULL);

    return;
}

