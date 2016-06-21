#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include <assert.h> 

#include "sshinner_s.h"


/*
 * Number of worker threads that have finished setting themselves up.
 */
static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;


void thread_bufferevent_cb(struct bufferevent *bev, short events, void *ptr)
{
    P_TRANS_ITEM p_trans = (P_TRANS_ITEM)ptr; 

    struct event_base *base = bufferevent_get_base(bev);
    int loop_terminate_flag = 0;

    //只有使用bufferevent_socket_connect进行的连接才会得到CONNECTED的事件
    if (events & BEV_EVENT_CONNECTED) 
    {
        st_d_print("GOT BEV_EVENT_CONNECTED event! ");
    } 
    else if (events & BEV_EVENT_ERROR) 
    {
        st_d_error("GOT BEV_EVENT_ERROR event[%d]! ", p_trans->usr_lport); 

        ss_free_trans(p_trans->p_activ_item, p_trans);
    } 
    else if (events & BEV_EVENT_EOF) 
    {
        st_d_print("GOT BEV_EVENT_EOF event[%d]! ", p_trans->usr_lport); 

        // 实际传输端结束，那么只针对这个传输，把对端的连接断开，
        // 由于TCP连接，那么对端的be_usr_srv和be_daemon_srv也会收到BEV_EVENT_EOF消息
        ss_free_trans(p_trans->p_activ_item, p_trans);

    }
    else if (events & BEV_EVENT_TIMEOUT) 
    {
        st_d_print("GOT BEV_EVENT_TIMEOUT event[%d]! ", p_trans->usr_lport); 

        ss_free_trans(p_trans->p_activ_item, p_trans);
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
 *  
 * 这里命令字段和数据字段分开处理，命令是自己解析，而数据需要转发，需要 
 * 为数据流程进行优化 
 */
void thread_bufferread_cb(struct bufferevent *bev, void *ptr)
{
    P_TRANS_ITEM p_trans = (P_TRANS_ITEM)ptr; 

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if (bev == p_trans->bev_u && p_trans->bev_d) 
    {
        st_d_print("转发数据包USR->DAEMON");
        bufferevent_write_buffer(p_trans->bev_d, bufferevent_get_input(bev));
    }
    else if (bev == p_trans->bev_d && p_trans->bev_u) 
    {
        st_d_print("转发数据包DAEMON->USR");
        bufferevent_write_buffer(p_trans->bev_u, bufferevent_get_input(bev));
    }
    else
    {
        SYS_ABORT("WRRRRRR!");
    }

    return;
}

void thread_bufferread_cb_enc(struct bufferevent *bev, void *ptr)
{
    P_TRANS_ITEM p_trans = (P_TRANS_ITEM)ptr; 

    ENC_FRAME from_f;
    ENC_FRAME to_f;

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if (bev == p_trans->bev_u && p_trans->bev_d) 
    {
        st_d_print("加密转发数据包USR->REMOTE");    // 解密

        from_f.len = bufferevent_read(p_trans->bev_u, from_f.dat, FRAME_SIZE); 
        if (from_f.len > 0)
        {
            decrypt(&p_trans->ctx_dec, &from_f, &to_f);
            bufferevent_write(p_trans->bev_d, to_f.dat, from_f.len);
        }        
        else
        {
            st_d_error("读取数据出错！");
        }

    }
    else if (bev == p_trans->bev_d && p_trans->bev_u) 
    {
        st_d_print("加密转发数据包REMOTE->USR");    // 加密

        from_f.len = bufferevent_read(p_trans->bev_d, from_f.dat, FRAME_SIZE);
        if (from_f.len > 0)
        {
            encrypt(&p_trans->ctx_enc, &from_f, &to_f);
            bufferevent_write(p_trans->bev_u, to_f.dat, to_f.len);
        }        
        else
        {
            st_d_error("读取数据出错！");
        }
    }
    else
    {
        SYS_ABORT("WRRRRRR!");
    }

    return;
}


static void *thread_run(void *arg);
static void thread_process(int fd, short which, void *arg);

static void wait_for_thread_registration(int nthreads) 
{
    while (init_count < nthreads) {
        pthread_cond_wait(&init_cond, &init_lock);
    }
}

static void register_thread_initialized(void) 
{
    pthread_mutex_lock(&init_lock);
    init_count++;
    st_d_print("线程数目[%d]OK!", init_count);
    pthread_cond_signal(&init_cond);
    pthread_mutex_unlock(&init_lock);
}

extern RET_T ss_create_worker_threads(size_t thread_num, P_THREAD_OBJ threads)
{
    int i = 0;

    pthread_attr_t  attr;
    int ret = 0;

    pthread_mutex_init(&init_lock, NULL);
    pthread_cond_init(&init_cond, NULL);

    for (i=0; i < thread_num; ++i)
    {
        int fds[2];
        if (pipe(fds)) {
            SYS_ABORT("Can't create notify pipe");
        }

        threads[i].notify_receive_fd = fds[0];
        threads[i].notify_send_fd = fds[1];

        threads[i].base = event_init();
        if (! threads[i].base ){
            SYS_ABORT("Can't allocate event base");
        }
        threads[i].p_notify_event = event_new(threads[i].base, threads[i].notify_receive_fd,
                                             EV_READ | EV_PERSIST, thread_process, &threads[i]);

        if (! threads[i].p_notify_event ){
            SYS_ABORT("Can't allocate new event");
        }

        if (event_add(threads[i].p_notify_event, 0) == -1) {
            SYS_ABORT("Can't monitor libevent notify pipe");
        }

        slist_init(&threads[i].conn_queue);
        pthread_mutex_init(&threads[i].q_lock, NULL); 

        pthread_attr_init(&attr);
        if ((ret = pthread_create(&threads[i].thread_id, &attr, thread_run, (void*)(&threads[i]))) != 0) 
        {
             SYS_ABORT("Cannot create worker thread %s", strerror(ret));
        }
    }


    pthread_mutex_lock(&init_lock);
    wait_for_thread_registration(thread_num);
    pthread_mutex_unlock(&init_lock);

}

static void *thread_run(void *arg) 
{
    P_THREAD_OBJ me = (P_THREAD_OBJ)arg;

    register_thread_initialized();

    event_base_loop(me->base, 0);

    return NULL;
}


/*
 * Processes an incoming "handle a new connection" item. This is called when
 * input arrives on the libevent wakeup pipe.
 */

/**
 * 这里有一个竞争条件：如果这里不能建立libevent连接，或者发送HD_CMD_SS5_ACT之前就收到了 
 * EOF的事件，那么客户端就会存在一个僵尸的trans连接，客户端目前是单线程的，必须消除这种 
 * 消耗 
 *  
 * 目前想到的处理方式就是，在拆除trans的同时，额外的向客户端主通道发送一个命令 
 */
static void thread_process(int fd, short which, void *arg) 
{
    P_THREAD_OBJ p_threadobj = (P_THREAD_OBJ)arg; 
    P_TRANS_ITEM p_trans = NULL;
    P_SLIST_HEAD p_list = NULL;
    P_C_ITEM p_c_item = NULL;
    struct bufferevent *new_bev = NULL;
    char buf[1];
    CTL_HEAD head;

    if (read(fd, buf, 1) != 1)
    {
        st_d_error("Can't read from libevent pipe\n");
        return;
    }

    switch (buf[0]) 
    {
        case 'D':   // DAEMON->USR
            p_list = slist_fetch(&p_threadobj->conn_queue);
            if (!p_list)
            {
                st_d_error("无法从任务队列中获取任务！");
                return;
            }

            p_c_item = list_entry(p_list, C_ITEM, list);
            p_trans = (P_TRANS_ITEM)p_c_item->arg.ptr; 

            new_bev = 
                bufferevent_socket_new(p_threadobj->base, p_c_item->socket, BEV_OPT_CLOSE_ON_FREE); 
            bufferevent_setcb(new_bev, thread_bufferread_cb, NULL, thread_bufferevent_cb, p_trans);
            bufferevent_enable(new_bev, EV_READ|EV_WRITE);

            p_trans->bev_d = new_bev;
            free(p_c_item);

            if (p_trans->bev_u == NULL || p_trans->usr_lport == 0 || p_trans->p_activ_item == NULL) 
            {
                SYS_ABORT("USR SIDE SHOULD BE OK ALREAY!!!");
            }

            st_d_print("WORKTHREAD-> DAEMON_USR(%d) OK!", p_trans->usr_lport); 
            st_d_print("DDDDD: 当前活动连接数：[[[ %d ]]]，任务队列：[[ %d ]]", 
                       slist_count(&p_trans->p_activ_item->trans), slist_count(&p_threadobj->conn_queue)); 

            st_d_print("激活客户端Bufferevent使能！");
            memset(&head, 0, CTL_HEAD_LEN);
            head.direct = USR_DAEMON; 
            head.cmd = HD_CMD_CONN_ACT; 
            head.extra_param = p_trans->usr_lport; 
            head.mach_uuid = p_trans->p_activ_item->mach_uuid; 
            bufferevent_write(p_trans->p_activ_item->bev_daemon, &head, CTL_HEAD_LEN); 
            head.direct = DAEMON_USR; 
            bufferevent_write(p_trans->p_activ_item->bev_usr, &head, CTL_HEAD_LEN); 

            break;

        case 'U':   //USR->DAEMON
            p_list = slist_fetch(&p_threadobj->conn_queue);
            if (!p_list)
            {
                st_d_error("无法从任务队列中获取任务！");
                return;
            }

            p_c_item = list_entry(p_list, C_ITEM, list);
            p_trans = (P_TRANS_ITEM)p_c_item->arg.ptr; 

            new_bev = 
                bufferevent_socket_new(p_threadobj->base, p_c_item->socket, BEV_OPT_CLOSE_ON_FREE); 
            bufferevent_setcb(new_bev, thread_bufferread_cb, NULL, thread_bufferevent_cb, p_trans);
            bufferevent_enable(new_bev, EV_READ|EV_WRITE);

            p_trans->bev_u = new_bev;
            free(p_c_item);

            st_d_print("WORKTHREAD-> USR_DAEMON(%d) OK!", p_trans->usr_lport); 

            break;

        case 'S':   // DAEMON->USR
            p_list = slist_fetch(&p_threadobj->conn_queue);
            if (!p_list)
            {
                st_d_error("无法从任务队列中获取任务！");
                return;
            }
            p_c_item = list_entry(p_list, C_ITEM, list);
            p_trans = (P_TRANS_ITEM)p_c_item->arg.ptr; 

            assert(p_trans->is_enc);
            assert(p_trans->dat); 

            encrypt_ctx_init(&p_trans->ctx_enc, p_trans->usr_lport, p_trans->p_activ_item->enc_key, 1); 
            encrypt_ctx_init(&p_trans->ctx_dec, p_trans->usr_lport, p_trans->p_activ_item->enc_key, 0);

            int remote_socket = 0;
            char* buf = (char *)p_trans->dat;

            if (buf[3] == 0x01)
            {
                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));

                sin.sin_family = AF_INET;
                memcpy(&sin.sin_addr.s_addr, &buf[4], 4);
                memcpy(&sin.sin_port, &buf[4+4], 2);
                free(p_trans->dat);

                st_d_print("REQUEST: %s:%d", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

                remote_socket = ss_connect_srv(&sin);
                if (remote_socket == -1)
                {
                    free(p_c_item);
                    st_d_error("CONNECT ERROR!");
                    return;
                }
            }
            else
            {
                char remote_addr[128];
                unsigned short remote_port = 0;
                memset(remote_addr, 0, sizeof(remote_addr));
                strncpy(remote_addr, &buf[4+1], buf[4]);
                memcpy(&remote_port, &buf[4+1+buf[4]], 2);
                free(p_trans->dat);

                P_DNS_STRUCT p_dns = (P_DNS_STRUCT)calloc(sizeof(DNS_STRUCT), 1);
                if (!p_dns)
                {
                    st_d_error("申请内存失败：%d", sizeof(DNS_STRUCT));
                    free(p_c_item);
                    return;
                }

                st_d_print("REQUEST: %s:%d", remote_addr, ntohs(remote_port));

                strncpy(p_dns->hostname, remote_addr, sizeof(p_dns->hostname));
                p_dns->port = remote_port;
                p_dns->p_c_item = p_c_item;
                p_dns->p_threadobj = p_threadobj;
                p_dns->p_trans = p_trans;

                struct evutil_addrinfo hints;
                struct evdns_getaddrinfo_request *req;
                memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET;
                hints.ai_flags = EVUTIL_AI_CANONNAME;
                /* Unless we specify a socktype, we'll get at least two entries for
                 * each address: one for TCP and one for UDP. That's not what we
                 * want. */
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_protocol = IPPROTO_TCP;


                req = evdns_getaddrinfo(
                    srvopt.evdns_base, remote_addr, NULL /* no service name given */, 
                                  &hints, dns_query_cb, p_dns);
                if (req == NULL) {
                  printf("    [request for %s returned immediately]\n", remote_addr);
                  /* No need to free user_data or decrement n_pending_requests; that
                   * happened in the callback. */
                }

                return;
            }

            evutil_make_socket_nonblocking(p_c_item->socket);
            struct bufferevent *new_bev = 
                bufferevent_socket_new(p_threadobj->base, p_c_item->socket, BEV_OPT_CLOSE_ON_FREE); 
            assert(new_bev);
            bufferevent_setcb(new_bev, thread_bufferread_cb_enc, NULL, thread_bufferevent_cb, p_trans);
            bufferevent_enable(new_bev, EV_READ|EV_WRITE);

            evutil_make_socket_nonblocking(remote_socket);
            struct bufferevent *new_ext_bev = 
                bufferevent_socket_new(p_threadobj->base, remote_socket , BEV_OPT_CLOSE_ON_FREE); 
            assert(new_ext_bev);
            bufferevent_setcb(new_ext_bev, thread_bufferread_cb_enc, NULL, thread_bufferevent_cb, p_trans);
            bufferevent_enable(new_ext_bev, EV_READ|EV_WRITE);

            p_trans->bev_d = new_bev;
            p_trans->bev_u = new_ext_bev;
            free(p_c_item);

            st_d_print("DDDDD: 当前活动连接数：[[[ %d ]]], 任务队列：[[ %d ]]", 
                       slist_count(&p_trans->p_activ_item->trans), slist_count(&p_threadobj->conn_queue)); 

            st_d_print("SS5激活客户端Bufferevent使能！");
            memset(&head, 0, CTL_HEAD_LEN);
            head.direct = USR_DAEMON; 
            head.cmd = HD_CMD_SS5_ACT; 
            head.extra_param = p_trans->usr_lport; 
            head.mach_uuid = p_trans->p_activ_item->mach_uuid; 
            bufferevent_write(p_trans->p_activ_item->bev_daemon, &head, CTL_HEAD_LEN); 
            break;

    default:
        SYS_ABORT("WHAT DO I GET: %c", buf[0]);
        break;
    }

    return;
}

void dns_query_cb(int errcode, struct evutil_addrinfo *addr, void *ptr)
{
    P_DNS_STRUCT p_dns = (P_DNS_STRUCT)ptr;

    if (errcode) 
    {
        printf("Query error for: %s -> %s\n", p_dns->hostname, evutil_gai_strerror(errcode)); 
    }
    else
    {
        struct evutil_addrinfo *ai;
        struct sockaddr_in sin;

        for (ai = addr; ai; ai = ai->ai_next) 
        {
            if (ai->ai_family == AF_INET) 
            {
                memset(&sin, 0, sizeof(sin));
                sin.sin_family = AF_INET;
                sin.sin_addr = ((struct sockaddr_in *)ai->ai_addr)->sin_addr;
                sin.sin_port = p_dns->port;

                st_d_print("REQUEST: %s:%d", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

                int remote_socket = ss_connect_srv(&sin);
                if (remote_socket == -1)
                {
                    st_d_error("REQUEST: %s:%d FAILED!", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
                    continue;
                }

                evutil_make_socket_nonblocking(p_dns->p_c_item->socket);
                struct bufferevent *new_bev = 
                    bufferevent_socket_new(p_dns->p_threadobj->base, p_dns->p_c_item->socket, BEV_OPT_CLOSE_ON_FREE); 
                assert(new_bev);
                bufferevent_setcb(new_bev, thread_bufferread_cb_enc, NULL, thread_bufferevent_cb, p_dns->p_trans);
                bufferevent_enable(new_bev, EV_READ|EV_WRITE);

                evutil_make_socket_nonblocking(remote_socket);
                struct bufferevent *new_ext_bev = 
                    bufferevent_socket_new(p_dns->p_threadobj->base, remote_socket , BEV_OPT_CLOSE_ON_FREE); 
                assert(new_ext_bev);
                bufferevent_setcb(new_ext_bev, thread_bufferread_cb_enc, NULL, thread_bufferevent_cb, p_dns->p_trans);
                bufferevent_enable(new_ext_bev, EV_READ|EV_WRITE);

                p_dns->p_trans->bev_d = new_bev;
                p_dns->p_trans->bev_u = new_ext_bev;

                st_d_print("DDDDD: 当前活动连接数：[[[ %d ]]], 任务队列：[[ %d ]]", 
                           slist_count(&p_dns->p_trans->p_activ_item->trans), 
                           slist_count(&p_dns->p_threadobj->conn_queue)); 


                st_d_print("SS5激活客户端Bufferevent使能！");
                CTL_HEAD head;
                memset(&head, 0, CTL_HEAD_LEN);
                head.direct = USR_DAEMON; 
                head.cmd = HD_CMD_SS5_ACT; 
                head.extra_param = p_dns->p_trans->usr_lport; 
                head.mach_uuid = p_dns->p_trans->p_activ_item->mach_uuid; 
                bufferevent_write(p_dns->p_trans->p_activ_item->bev_daemon, &head, CTL_HEAD_LEN); 

                break;

            } 

            st_d_print("ALL REQUEST FOR %s FAILED!", p_dns->hostname); 

        }
        evutil_freeaddrinfo(addr);
    }

    free(p_dns->p_c_item);
    free(p_dns);

    return;
}


/**
 * 
 * 用主管道命令的方式，发送结束TRANS信息 
 * 多次调用该命令是无害的，因为客户端检测不到对应的trans就忽略之 
 */
void ss_cmd_end_trans(P_TRANS_ITEM p_trans)
{
    if (!p_trans || !p_trans->p_activ_item || !p_trans->p_activ_item->bev_daemon) 
    {
        return;
    }

    CTL_HEAD head;
    memset(&head, 0, CTL_HEAD_LEN);

    head.direct = USR_DAEMON; 
    head.cmd = HD_CMD_END_TRANS; 
    head.extra_param = p_trans->usr_lport; 
    head.mach_uuid = p_trans->p_activ_item->mach_uuid; 
    bufferevent_write(p_trans->p_activ_item->bev_daemon, &head, CTL_HEAD_LEN); 

    return;
}

