#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include <assert.h> 

#include "sshinner_s.h"


void main_bufferevent_cb(struct bufferevent *bev, short events, void *ptr)
{
    P_ACTIV_ITEM p_item = NULL;
    P_THREAD_OBJ p_threadobj = NULL;

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
 * 监听套接字响应事件 
 *  
 * 这里需要接受客户端的一次数据，然后决定将这个客户端分配到 
 * 哪个线程去处理，所以这里只进行一次call_back 
 */
void accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{ 
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

    getnameinfo (address, socklen,
               hbuf, sizeof(hbuf),sbuf, sizeof(sbuf),
               NI_NUMERICHOST | NI_NUMERICSERV);
    st_print("侦测到客户端连接请求 (HOST=%s, PORT=%s)\n", hbuf, sbuf);

    /* We got a new connection! Set up a bufferevent for it. */
    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = 
        bufferevent_socket_new(base, fd, 0 /*BEV_OPT_CLOSE_ON_FREE*/);

    /**
     * 对于服务端，一般都是阻塞在读，而如果要写，一般在read_cb中写回就可以了 
     */
    bufferevent_setcb(bev, main_bufferread_cb, NULL, main_bufferevent_cb, NULL);
    bufferevent_enable(bev, EV_READ|EV_WRITE);

    st_d_print("侦听套接字创建侦听事件OK ...");

    return;
}

void accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();

    st_d_error( "侦听套接字错误：%s", evutil_socket_error_to_string(err));
    // event_base_loopexit(base, NULL);
    return;
}


/**
 * 读取事件，主要进行数据转发 
 *  
 * 这里命令字段和数据字段分开处理，命令是自己解析，而数据需要转发，需要 
 * 为数据流程进行优化 
 */
void main_bufferread_cb(struct bufferevent *bev, void *ptr)
{
    size_t n = 0;
    PKG_HEAD head;
    RET_T  ret;

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if ( evbuffer_remove(input, &head, HEAD_LEN) != HEAD_LEN)
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
        ret = ss_main_handle_ctl(bev, &head, (char *)dat);
        if (ret == RET_NO)
            ss_ret_cmd_err(bev, head.mach_uuid, 
                           head.direct == USR_DAEMON? DAEMON_USR: USR_DAEMON);
        free(dat);
    }
    else
    {
        SYS_ABORT("侦听套接字只能处理命令请求：%d", head.type); 
    }

    return;
}



static RET_T ss_main_handle_ctl(struct bufferevent *bev, 
                           P_PKG_HEAD p_head, char* dat)
{
    json_object *new_obj = json_tokener_parse(dat);
    json_object *p_store_obj = NULL;

    P_THREAD_OBJ    p_threadobj = NULL;
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

    // USR->DAEMON
    if (p_head->direct == USR_DAEMON) 
    {
        json_fetch_and_copy(new_obj, "r_mach_uuid", uuid_s, sizeof(uuid_s));
        if (sd_id128_from_string(uuid_s, &mach_uuid) != 0)
        {
            st_d_error("UUID转换失败：%s！", uuid_s);
            goto error_ret;
        }

        p_threadobj = ss_get_threadobj(mach_uuid);

        p_acct_item = ss_find_acct_item(&srvopt, username, userid);
        if (!p_acct_item)
        {
            st_d_print("账户 %s:%lu 不存在！", username, userid);
            goto error_ret;
        }

        p_activ_item = ss_uuid_search(&p_threadobj->uuid_tree, mach_uuid);

        if (!p_activ_item)
        {
            st_d_print("会话 %s:%lu %s 不存在！", username, userid,
                        SD_ID128_CONST_STR(mach_uuid));
            goto error_ret;
        }

        bufferevent_free(bev);

        P_C_ITEM p_c = (P_C_ITEM)calloc(sizeof(C_ITEM), 1);
        if (!p_c)
        {
            st_d_error("申请内存[%d]失败！", sizeof(C_ITEM));
            goto error_ret;
        }
        p_c->socket = bufferevent_getfd(bev);
        p_c->arg.ptr = p_activ_item;

        slist_add(&p_c->list, &p_threadobj->conn_queue);
        write(p_threadobj->notify_send_fd, "U", 1);

        st_d_print("已将%s:%lu %s加入线程[%lu]处理队列！", username, userid,
                   SD_ID128_CONST_STR(p_activ_item->mach_uuid), p_threadobj->thread_id); 

        return RET_YES;
    }
    // DAEMON->USR
    else if (p_head->direct == DAEMON_USR) 
    {
        p_threadobj = ss_get_threadobj(p_head->mach_uuid);
        p_acct_item = ss_find_acct_item(&srvopt, username, userid);
        if (!p_acct_item)
        {
            st_d_print("%s:%lu 用户尚未存在，创建之...", username, userid);

            p_acct_item = (P_ACCT_ITEM)malloc(sizeof(ACCT_ITEM));
            if (!p_acct_item)
            {
                st_d_print("申请内存[%d]失败！");
                goto error_ret;
            }

            strncpy(p_acct_item->username, username, sizeof(p_acct_item->username));
            p_acct_item->userid = userid;
            slist_init(&p_acct_item->items);

            slist_add(&p_acct_item->list, &srvopt.acct_items);
        }

        if (ss_uuid_search(&p_threadobj->uuid_tree, p_head->mach_uuid))
        {
            st_d_print("会话 %s:%lu %s 已经存在！", username, userid,
                       SD_ID128_CONST_STR(p_head->mach_uuid));
            goto error_ret;
        }

        p_activ_item = (P_ACTIV_ITEM)calloc(sizeof(ACTIV_ITEM), 1);
        if (!p_activ_item)
        {
            st_d_print("申请内存[%d]失败！", sizeof(ACTIV_ITEM));
           goto error_ret;
        }

        bufferevent_free(bev);

        p_activ_item->base = p_threadobj->base; 
        p_activ_item->mach_uuid = p_head->mach_uuid;

        P_C_ITEM p_c = (P_C_ITEM)calloc(sizeof(C_ITEM), 1);
        if (!p_c)
        {
            st_d_error("申请内存[%d]失败！", sizeof(C_ITEM));
            free(p_activ_item);
            goto error_ret;
        }
        p_c->socket = bufferevent_getfd(bev);
        p_c->arg.ptr = p_activ_item;


        slist_add(&p_activ_item->list, &p_acct_item->items);
        ss_uuid_insert(&p_threadobj->uuid_tree, p_activ_item);

        slist_add(&p_c->list, &p_threadobj->conn_queue);
        write(p_threadobj->notify_send_fd, "D", 1);

#if 0     

        p_acct_item = NULL;
        p_activ_item = NULL;
        if (p_acct_item = ss_find_acct_item(&srvopt, username, userid)) 
        {
            st_d_print("%s:%lu", p_acct_item->username, p_acct_item->userid); 
        }
        if (p_activ_item = ss_uuid_search(&srvopt.uuid_tree, p_head->mach_uuid))
        {
            st_d_print("%s", SD_ID128_CONST_STR(p_activ_item->mach_uuid));
        }
#endif


        st_d_print("已将%s:%lu %s加入线程[%lu]处理队列！", username, userid,
                   SD_ID128_CONST_STR(p_activ_item->mach_uuid),p_threadobj->thread_id); 

        return RET_YES;
    }

error_ret:
    json_object_put(new_obj);

    return RET_NO;
}
