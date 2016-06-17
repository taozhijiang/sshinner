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

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "rbtree.h"

#include "st_others.h"
#include "st_slist.h"
#include "sshinner.h"

#define MAX_TRANS_NUM   500

struct _activ_item;
typedef struct _trans_item {
    struct _activ_item*   p_activ_item;
    unsigned short usr_lport;       //USR本地连接的端口，作为标示
    struct bufferevent *bev_u;
    struct bufferevent *bev_d;
    void*  dat;
} TRANS_ITEM, *P_TRANS_ITEM;

typedef struct _activ_item {
    SLIST_HEAD         list;
    struct rb_node     node;
    struct event_base  *base;
    sd_id128_t         mach_uuid;   // DEAMON机器的会话ID
    struct bufferevent *bev_daemon; // 控制信息传输通道
    struct bufferevent *bev_usr;
    TRANS_ITEM          trans[MAX_TRANS_NUM];
    unsigned long       pkg_cnt;    // 转发的数据包计数
} ACTIV_ITEM, *P_ACTIV_ITEM;

typedef struct _acct_item {
    char username   [128];      //
    unsigned long   userid;
    SLIST_HEAD      list;       //自身链表
    SLIST_HEAD      items;    
} ACCT_ITEM, *P_ACCT_ITEM;

/* A connection queue. */
typedef struct conn_item {
    SLIST_HEAD      list;
    int             socket;
    union {
        unsigned long dat;
        void*         ptr;
    } arg;
}C_ITEM, *P_C_ITEM;

struct event;
typedef struct _thread_obj {
    pthread_t thread_id;        /* unique ID of this thread */
    struct event_base *base;    /* libevent handle this thread uses */
    struct event *p_notify_event;  /* listen event for notify pipe */
    int notify_receive_fd;      /* receiving end of notify pipe */
    int notify_send_fd;         /* sending end of notify pipe */
    struct rb_root  uuid_tree;
    pthread_mutex_t q_lock;
    SLIST_HEAD   conn_queue;    /* queue of new connections to handle */
} THREAD_OBJ, *P_THREAD_OBJ;


static const char* PRIVATE_KEY_FILE = "./ssl/private.key";

typedef struct _srv_opt
{
    pthread_t       main_thread_id;   
    unsigned short  port;
    RSA *           p_prikey;  //服务器用的解密私钥
    SLIST_HEAD      acct_items;
    int             thread_num;
    P_THREAD_OBJ    thread_objs;
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

void main_bufferread_cb(struct bufferevent *bev, void *ptr);
void main_bufferevent_cb(struct bufferevent *bev, short events, void *ptr);
static RET_T ss_main_handle_init(struct bufferevent *bev, 
                           P_CTL_HEAD p_head, char* dat);
static RET_T ss_main_handle_ss5(struct bufferevent *bev, 
                           P_CTL_HEAD p_head, void* dat);
static RET_T ss_main_handle_ctl(struct bufferevent *bev, P_CTL_HEAD p_head);
RET_T ss_send_head_cmd(struct bufferevent *bev, P_CTL_HEAD p_head,
                       int cmd, enum DIREC direct, unsigned long extra_param);

/**
 * 数据转发和处理类函数
 */
extern RET_T ss_create_worker_threads(size_t thread_num, P_THREAD_OBJ threads);
extern void thread_bufferevent_cb(struct bufferevent *bev, short events, void *ptr);
extern void thread_bufferread_cb(struct bufferevent *bev, void *ptr);
/* 简易从服务器发送控制信息 */


extern void ss_ret_cmd_ok(struct bufferevent *bev,
                          sd_id128_t uuid, enum DIREC direct);
extern void ss_ret_cmd_err(struct bufferevent *bev,
                           sd_id128_t uuid, enum DIREC direct);


extern SRV_OPT srvopt;
extern struct  event_base *main_base;

/**
 * UUID到线程池索引的映射
 */
static inline P_THREAD_OBJ ss_get_threadobj(sd_id128_t uuid)
{
    return (&srvopt.thread_objs[(uuid.bytes[0] + uuid.bytes[7]) % srvopt.thread_num] ); 
}

extern RET_T ss_acct_remove(P_SRV_OPT p_srvopt, P_ACCT_ITEM p_item);
extern RET_T ss_activ_item_remove(P_SRV_OPT p_srvopt,
                                  P_THREAD_OBJ p_threadobj, P_ACTIV_ITEM p_item);

#endif
