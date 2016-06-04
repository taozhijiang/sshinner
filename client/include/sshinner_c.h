#ifndef _SSHINNER_C_H
#define _SSHINNER_C_H

#include <stdio.h>

#include <systemd/sd-id128.h> 

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/listener.h>
#include <event2/util.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/bufferevent.h>


#include "sshinner.h"
#include "st_others.h"
#include "st_slist.h"


enum CLT_TYPE {
    C_DAEMON, C_USR,
};


typedef struct _portmap {
    unsigned short usrport;
    unsigned short daemonport;
    struct bufferevent *bev;
} PORTMAP, *P_PORTMAP;


#define MAX_PORTMAP_NUM 10


typedef struct _clt_opt
{
    enum CLT_TYPE       C_TYPE;
    sd_id128_t          mach_uuid;
    char hostname[128];
    char username[128];  
    unsigned long   userid;
    struct sockaddr_in  srv;
    struct bufferevent *srv_bev;

    sd_id128_t      session_uuid;
    PORTMAP         maps[MAX_PORTMAP_NUM];
}CLT_OPT, *P_CLT_OPT;


extern struct event_base *base;
extern CLT_OPT cltopt;

/**
 * 通用类函数
 */
extern RET_T load_settings_client(P_CLT_OPT p_opt);
extern void dump_clt_opts(P_CLT_OPT p_opt);


/**
 * 客户端与USR/DAEMON通信类函数
 */
// 连接函数
void bufferevent_cb(struct bufferevent *bev, short events, void *ptr);
void accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx);
void accept_error_cb(struct evconnlistener *listener, void *ctx);
// 数据处理类函数
void bufferread_cb(struct bufferevent *bev, void *ptr);


/**
 * 客户端与SRV的通信
 */
void srv_bufferread_cb(struct bufferevent *bev, void *ptr);
P_PORTMAP sc_find_portmap(unsigned short usrport);
P_PORTMAP sc_find_create_portmap(unsigned short daemonport);

#endif
