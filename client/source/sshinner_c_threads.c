#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include <assert.h> 

#include "sshinner_c.h"


/*
 * Number of worker threads that have finished setting themselves up.
 */
static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;


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

extern RET_T sc_create_ss5_worker_threads(size_t thread_num, P_THREAD_OBJ threads)
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
static void thread_process(int fd, short which, void *arg) 
{
    P_THREAD_OBJ p_threadobj = (P_THREAD_OBJ)arg; 

    P_SLIST_HEAD p_list = NULL;
    P_C_ITEM p_c_item = NULL;
    P_PORTTRANS p_trans = NULL;

    char buf[1];
    CTL_HEAD head;

    if (read(fd, buf, 1) != 1)
    {
        st_d_error("Can't read from libevent pipe\n");
        return;
    }

    switch (buf[0]) 
    {
        case 'Q': 
            p_list = slist_fetch(&p_threadobj->conn_queue);
            if (!p_list)
            {
                st_d_error("无法从任务队列中获取任务！");
                return;
            }

            p_c_item = list_entry(p_list, C_ITEM, list);
            p_trans = (P_PORTTRANS)p_c_item->arg.ptr; 

            int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
            if(sc_connect_srv(srv_fd) != RET_YES) 
            {
                st_d_error("连接服务器失败！");
                return;
            }

            if (sc_daemon_ss5_init_srv(srv_fd, p_c_item->buf, p_trans->l_port) != RET_YES) 
            {
                p_trans->l_port = 0;
                close(srv_fd);
                st_d_error("服务器返回错误!");
                return;
            }

            evutil_make_socket_nonblocking(p_c_item->socket); 
            struct bufferevent *local_bev = 
                bufferevent_socket_new(p_threadobj->base, p_c_item->socket, BEV_OPT_CLOSE_ON_FREE); 
            assert(local_bev);
            bufferevent_setcb(local_bev, ss5_bufferread_cb_enc, NULL, ss5_bufferevent_cb, p_trans);
            //fferevent_enable(local_bev, EV_READ|EV_WRITE);

            evutil_make_socket_nonblocking(srv_fd); 
            struct bufferevent *new_bev = 
                bufferevent_socket_new(p_threadobj->base, srv_fd, BEV_OPT_CLOSE_ON_FREE); 
            bufferevent_setcb(new_bev, ss5_bufferread_cb_enc, NULL, ss5_bufferevent_cb, p_trans); 
            bufferevent_enable(new_bev, EV_READ|EV_WRITE);


            encrypt_ctx_init(&p_trans->ctx_enc, p_trans->l_port, cltopt.enc_key, 1); 
            encrypt_ctx_init(&p_trans->ctx_dec, p_trans->l_port, cltopt.enc_key, 0);

            p_trans->local_bev = local_bev;
            p_trans->srv_bev = new_bev;

            st_d_print("THREAD FOR (%d) OK!", p_trans->l_port); 

            break;

    default:
        SYS_ABORT("WHAT DO I GET: %c", buf[0]);
        break;
    }

    return;
}

