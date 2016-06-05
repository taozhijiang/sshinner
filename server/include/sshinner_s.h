#ifndef _SSHINNER_S_H
#define _SSHINNER_C_H

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


#include "rbtree.h"

#include "st_others.h"
#include "st_slist.h"
#include "sshinner.h"


typedef struct _activ_item {
    SLIST_HEAD         list;
    struct rb_node     node;
    struct event_base  *base;
    sd_id128_t         mach_uuid;   // DEAMON机器的会话ID
    struct bufferevent *bev_daemon;
    struct bufferevent *bev_usr;
    unsigned int        daemon_ttl; // 看门狗，存活的时间　５次
    unsigned int        usr_ttl;    // 看门狗
} ACTIV_ITEM, *P_ACTIV_ITEM;

typedef struct _acct_item {
    char username   [128];  //A
    unsigned long   userid;
    SLIST_HEAD      list;   //自身链表
    SLIST_HEAD      items;  //activ链表头
} ACCT_ITEM, *P_ACCT_ITEM;

typedef struct _srv_opt
{
    unsigned short port;
    struct rb_root uuid_tree;
    SLIST_HEAD     acct_items;
}SRV_OPT, *P_SRV_OPT;


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
static RET_T ss_new_connect_from_daemon(P_PKG_HEAD p_head, char* dat);
static RET_T ss_new_connect_from_usr(P_PKG_HEAD p_head, char* dat);

static RET_T ss_handle_ctl(struct bufferevent *bev, 
                           P_PKG_HEAD p_head, char* dat);
static RET_T ss_handle_dat(struct bufferevent *bev,
                           P_PKG_HEAD p_head, void* dat);

/* 简易从服务器发送控制信息 */
static void ss_ret_cmd_ok(struct bufferevent *bev,
                          sd_id128_t uuid, enum DIREC direct);
static void ss_ret_cmd_err(struct bufferevent *bev,
                           sd_id128_t uuid, enum DIREC direct);
static void ss_ret_dat_err(struct bufferevent *bev,
                           sd_id128_t uuid, enum DIREC direct);
static void ss_ret_cmd_keep(struct bufferevent *bev,
                            sd_id128_t uuid, enum DIREC direct);

#endif
