#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include <assert.h> 

#include "sshinner_s.h"


void thread_bufferevent_cb(struct bufferevent *bev, short events, void *ptr)
{
    P_ACTIV_ITEM p_item = NULL;

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

        /**
         * 断开连接的时候拆除连接
         */
        if (ptr)
        {
            p_item = (P_ACTIV_ITEM)ptr;

            if (p_item->bev_daemon == bev) 
            {
                st_d_print("Daemon端断开连接！");
                if (p_item->bev_usr) 
                {
                    st_d_print("拆除Usr端！");
                    bufferevent_free(p_item->bev_usr); 
                }
                bufferevent_free(p_item->bev_daemon); 
                p_item->bev_daemon = NULL;
                p_item->bev_usr = NULL; 

            }
            // USR端主动断开后，Daemon端检测到断开消息后，会自动重连服务
            else if (p_item->bev_usr == bev) 
            {
                st_d_print("Usr端断开连接！");
                if (p_item->bev_daemon) 
                {
                    bufferevent_free(p_item->bev_daemon); 
                    st_d_print("拆除Daemon端！");
                }
                bufferevent_free(p_item->bev_usr); 
                p_item->bev_daemon = NULL;
                p_item->bev_usr = NULL; 
            }
            else
            {
                SYS_ABORT("请检查BEV！！！");
            }

            /**
             * 清除这个链接
             */
            P_THREAD_OBJ p_threadobj = ss_get_threadobj(p_item->mach_uuid);
            ss_activ_item_remove(&srvopt, p_threadobj, p_item);
        }
        else
        {
            bufferevent_free(bev);
        }
    }
    else if (events & BEV_EVENT_TIMEOUT) 
    {
        st_d_print("GOT BEV_EVENT_TIMEOUT event! ");
    } 

    /*
    else if (events & BEV_EVENT_READING) 
    {
        st_d_print("GOT BEV_EVENT_READING event! ");
    } 
    else if (events & BEV_EVENT_WRITING) 
    {
        st_d_print("GOT BEV_EVENT_WRITING event! ");
    }
    */

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
    size_t n = 0;
    PKG_HEAD head;
    RET_T  ret;

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if ( evbuffer_remove(input, &head, HEAD_LEN) != HEAD_LEN )
    {
        st_d_print("读取数据包头%d错误!", HEAD_LEN);
        return;
    }

    if (head.type == 'C') 
    {
        void *dat = NULL;
        if (head.dat_len > 0) 
        {       
            if (!(dat = malloc(head.dat_len)) )
            {
                st_d_error("分配内存[%d]失败！", head.dat_len); 
                return;
            }
            
            memset(dat, 0, head.dat_len);
            size_t offset = 0;
            while ((n = evbuffer_remove(input, dat+offset, head.dat_len-offset)) > 0) 
            {
                if (n < (head.dat_len-offset)) 
                {
                    offset += n;
                    continue;
                }
                else
                break;
            }

            ulong crc = crc32(0L, dat, head.dat_len);
            if (crc != head.crc) 
            {
                st_d_error("数据包校验失败: %lu-%lu", crc, head.crc); 
                st_d_print("=>%s", (char*) dat);
                free(dat);
                return;
            }
        }
        ret = ss_handle_ctl(bev, &head, (char *)dat);
        if (ret == RET_NO)
            ss_ret_cmd_err(bev, head.mach_uuid, 
                           head.direct == USR_DAEMON? DAEMON_USR: USR_DAEMON);
        free(dat);
    }
    else if (head.type == 'D')
    {
        ret = ss_handle_dat(bev, &head);
        if (ret == RET_NO)
            ss_ret_dat_err(bev, head.mach_uuid, 
                           head.direct == USR_DAEMON? DAEMON_USR: USR_DAEMON);
    }
    else
    {
        SYS_ABORT("非法数据包类型： %c！", head.type); 
    }

    return;
}


static RET_T ss_handle_ctl(struct bufferevent *bev, 
                           P_PKG_HEAD p_head, char* dat)
{
    json_object *new_obj = json_tokener_parse(dat);
    json_object *p_store_obj = NULL;
    P_ACCT_ITEM     p_acct_item = NULL;
    P_ACTIV_ITEM    p_activ_item = NULL;

    char username   [128];
    unsigned long   userid;
    char uuid_s     [33];
    sd_id128_t      mach_uuid;
    
    if (!new_obj)
    {
        st_d_error("Json parse error: %s", dat);
        return RET_NO;
    }

    json_fetch_and_copy(new_obj, "username", username, sizeof(username));
    userid = json_object_get_int(json_object_object_get(new_obj,"userid"));


    /**
     * TODO:
     */
    // USR->DAEMON
    if (p_head->direct == USR_DAEMON) 
    {
   
        return RET_YES;
    }
    // DAEMON->USR
    else if (p_head->direct == DAEMON_USR) 
    {
        
        return RET_YES;
    }
}


static RET_T ss_handle_dat(struct bufferevent *bev,
                           P_PKG_HEAD p_head)
{
    char h_buff[4096];  /*libevent底层一次也就读取这么多*/
    size_t n = 0;
    P_ACTIV_ITEM p_activ_item = NULL;

    P_THREAD_OBJ p_threadobj = ss_get_threadobj(p_head->mach_uuid); 

    p_activ_item = ss_uuid_search(&p_threadobj->uuid_tree, p_head->mach_uuid);
    if (!p_activ_item)
    {
        st_d_error("会话UUID %s未找到！", SD_ID128_CONST_STR(p_head->mach_uuid));
        return RET_NO;
    }

    memset(h_buff, 0, sizeof(h_buff));
    memcpy(h_buff, p_head, HEAD_LEN);

     // USR->DAEMON
    if (p_head->direct == USR_DAEMON) 
    {
        if (bev != p_activ_item->bev_usr ||
            !p_activ_item->bev_daemon) 
        {
            st_d_error("BEV检查出错！");
            return RET_NO;
        }

        if(bufferevent_read(bev, GET_PKG_BODY(h_buff), p_head->dat_len) == p_head->dat_len)
        {
            bufferevent_write(p_activ_item->bev_daemon, 
                                h_buff, HEAD_LEN + p_head->dat_len);
            st_d_print("TRANSFORM FROM USR->DAEMON %d bytes", p_head->dat_len); 
        }
        else
        {
            st_d_error("读取Usr端数据负载失败！");
            return RET_NO;
        }

    }
    else if (p_head->direct == DAEMON_USR) 
    {
        if (bev != p_activ_item->bev_daemon ||
            !p_activ_item->bev_usr) 
        {
            st_d_error("BEV检查出错！");
            return RET_NO;
        }

        if(bufferevent_read(bev, GET_PKG_BODY(h_buff), p_head->dat_len) == p_head->dat_len)
        {
            bufferevent_write(p_activ_item->bev_usr, 
                                h_buff, HEAD_LEN + p_head->dat_len);
            st_d_print("TRANSFORM FROM DAEMON->USR %d bytes", p_head->dat_len); 
        }
        else
        {
            st_d_error("读取Daemon端数据负载失败！");
            return RET_NO;
        }
    }

    return RET_YES;
}

static void *thread_run(void *arg);
static void thread_process(int fd, short which, void *arg);

extern RET_T ss_create_worker_threads(size_t thread_num, P_THREAD_OBJ threads)
{
    int i = 0;

    pthread_attr_t  attr;
    int ret = 0;

    for (i=0; i < thread_num; ++i)
    {
        int fds[2];
        if (pipe(fds)) {
            SYS_ABORT("Can't create notify pipe");
        }

        threads[i].notify_receive_fd = fds[0];
        threads[i].notify_send_fd = fds[1];

        threads[i].uuid_tree = RB_ROOT;
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
}

static void *thread_run(void *arg) 
{
    P_THREAD_OBJ me = (P_THREAD_OBJ)arg;

    event_base_loop(me->base, 0);

    return NULL;
}


/*
 * Processes an incoming "handle a new connection" item. This is called when
 * input arrives on the libevent wakeup pipe.
 */
static void thread_process(int fd, short which, void *arg) 
{
    P_THREAD_OBJ p_threadobj = (P_THREAD_OBJ)arg; 
    P_ACTIV_ITEM p_activ_item = NULL;
    P_SLIST_HEAD p_list = NULL;
    P_C_ITEM p_c_item = NULL;
    struct bufferevent *new_bev = NULL;
    char buf[1];

    if (read(fd, buf, 1) != 1)
    {
        st_d_error("Can't read from libevent pipe\n");
        return;
    }

    switch (buf[0]) 
    {
        case 'D':
            p_list = slist_fetch(&p_threadobj->conn_queue);
            p_c_item = list_entry(p_list, C_ITEM, list);
            p_activ_item = (P_ACTIV_ITEM)p_c_item->arg.ptr;

            new_bev = 
                bufferevent_socket_new(p_activ_item->base, p_c_item->socket, BEV_OPT_CLOSE_ON_FREE);
            bufferevent_setcb(new_bev, thread_bufferread_cb, NULL, thread_bufferevent_cb, p_activ_item);
            bufferevent_enable(new_bev, EV_READ|EV_WRITE);

            p_activ_item->bev_daemon = new_bev;

            //　回复DAEMON OK
            ss_ret_cmd_ok(new_bev, p_activ_item->mach_uuid, USR_DAEMON);
            free(p_c_item);

            break;

        case 'U':
            p_list = slist_fetch(&p_threadobj->conn_queue);
            p_c_item = list_entry(p_list, C_ITEM, list);
            p_activ_item = (P_ACTIV_ITEM)p_c_item->arg.ptr;


            new_bev = 
                bufferevent_socket_new(p_activ_item->base, p_c_item->socket, BEV_OPT_CLOSE_ON_FREE);
            bufferevent_setcb(new_bev, thread_bufferread_cb, NULL, thread_bufferevent_cb, p_activ_item);
            bufferevent_enable(new_bev, EV_READ|EV_WRITE);

            p_activ_item->bev_usr = new_bev;

            //　回复USR OK
            ss_ret_cmd_ok(new_bev, p_activ_item->mach_uuid, DAEMON_USR);
            free(p_c_item);

            break;
    }

    return;
}

