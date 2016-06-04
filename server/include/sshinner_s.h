#include <errno.h>
#include <stdio.h>

#include <zlib.h>

#include <systemd/sd-id128.h> 

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/listener.h>
#include <event2/util.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/bufferevent.h>

#include "st_others.h"
#include "st_slist.h"
#include "sshinner.h"


typedef struct {
    pthread_t thread_id;        /* unique ID of this thread */
    struct event_base *base;    /* libevent handle this thread uses */
    int notify_receive_fd;      /* receiving end of notify pipe */
    int notify_send_fd;         /* sending end of notify pipe */
} WORKER_THREAD;


typedef struct _srv_opt
{
    unsigned short port;
    WORKER_THREAD  threads[5];
    SLIST_HEAD     acct;
}SRV_OPT, *P_SRV_OPT;


enum CLT_TYPE {
    C_DAEMON, C_USR,
};

typedef struct _conn_item {
    enum CLT_TYPE c_type;
    int  sk_in;
    int  sk_out;
} CONN_ITEM, *P_CONN_ITEM;

typedef struct _acct_item {
    SLIST_HEAD      list;
    char username   [128];  //A
    unsigned long   userid;
    CONN_ITEM conns[10];
} ACCT_ITEM, *P_ACCT_ITEM;

typedef struct _active_item {
    struct event_base *base;
    sd_id128_t  mach_uuid;   //B
    P_CONN_ITEM p_daemon;
    P_CONN_ITEM p_usr;
} ACTIVE_ITEM, *P_ACTIVE_ITEM;

/**
 * 工具类函数
 */
extern RET_T load_settings_server(P_SRV_OPT p_opt);
extern void dump_srv_opts(P_SRV_OPT p_opt);

/** 
 * 处理连接请求类函数 
 */
void accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx);
void accept_error_cb(struct evconnlistener *listener, void *ctx);
void bufferevent_cb(struct bufferevent *bev, short events, void *ptr);


/**
 * 数据转发和处理类函数
 */
void bufferread_cb(struct bufferevent *bev, void *ptr);
