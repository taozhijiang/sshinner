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
    P_TRANS_ITEM p_trans = (P_TRANS_ITEM)ptr; 

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if (bev == p_trans->bev_u && p_trans->bev_d) 
    {
        bufferevent_write_buffer(p_trans->bev_d, bufferevent_get_input(bev));
    }
    else if (bev == p_trans->bev_d && p_trans->bev_u) 
    {
        bufferevent_write_buffer(p_trans->bev_u, bufferevent_get_input(bev));
    }
    else
    {
        SYS_ABORT("WRRRRRR!");
    }

    return;
}



static RET_T ss_handle_dat(struct bufferevent *bev,
                           P_CTL_HEAD p_head)
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

    return RET_YES;
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
static void thread_process(int fd, short which, void *arg) 
{
    P_THREAD_OBJ p_threadobj = (P_THREAD_OBJ)arg; 
    P_TRANS_ITEM p_trans = NULL;
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
            if (!p_list)
            {
                st_d_error("无法从任务队列中获取任务！");
                return;
            }

            p_c_item = list_entry(p_list, C_ITEM, list);
            if (p_c_item->direct != DAEMON_USR) 
            {
                SYS_ABORT("数据流向错误！！！");
            }


            p_trans = (P_TRANS_ITEM)p_c_item->arg.ptr; 

            new_bev = 
                bufferevent_socket_new(p_threadobj->base, p_c_item->socket, BEV_OPT_CLOSE_ON_FREE); 
            bufferevent_setcb(new_bev, thread_bufferread_cb, NULL, thread_bufferevent_cb, p_trans);
            bufferevent_enable(new_bev, EV_READ|EV_WRITE);

            p_trans->bev_d = new_bev;
            free(p_c_item);

            break;

        case 'U':
            p_list = slist_fetch(&p_threadobj->conn_queue);
            if (!p_list)
            {
                st_d_error("无法从任务队列中获取任务！");
                return;
            }

            p_c_item = list_entry(p_list, C_ITEM, list);
            if (p_c_item->direct != USR_DAEMON) 
            {
                SYS_ABORT("数据流向错误！！！");
            }

            p_trans = (P_TRANS_ITEM)p_c_item->arg.ptr; 

            new_bev = 
                bufferevent_socket_new(p_threadobj->base, p_c_item->socket, BEV_OPT_CLOSE_ON_FREE); 
            bufferevent_setcb(new_bev, thread_bufferread_cb, NULL, thread_bufferevent_cb, p_trans);
            bufferevent_enable(new_bev, EV_READ|EV_WRITE);

            p_trans->bev_u = new_bev;
            free(p_c_item);

            break;
    }

    return;
}

