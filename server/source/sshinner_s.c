#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "sshinner_s.h"

extern RET_T load_settings_server(P_SRV_OPT p_opt)
{
    json_object *p_obj = NULL;
    json_object *p_class = NULL;
    json_object *p_store_obj = NULL;

    if (!p_opt)
        return RET_NO;

    if( ! (p_obj = json_object_from_file("settings.json")) )
        return RET_NO;

    if(json_object_object_get_ex(p_obj,"server",&p_class))
    {
        st_d_print("handling server configuration....");

        if (json_object_object_get_ex(p_class,"port",&p_store_obj))
            p_opt->port = json_object_get_int(p_store_obj); 

        json_object_put(p_obj);
        return RET_YES;
    }

    json_object_put(p_obj);
    return RET_NO;
}


extern void dump_srv_opts(P_SRV_OPT p_opt)
{
    if (!p_opt)
        return;

    st_d_print("PORT:%d", p_opt->port);

    return;
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
 * 监听套接字响应事件
 */
void accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{ 
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

    getnameinfo (address, socklen,
               hbuf, sizeof(hbuf),sbuf, sizeof(sbuf),
               NI_NUMERICHOST | NI_NUMERICSERV);

    st_print("WELCOME NEW CONNECTION FROM (HOST=%s, PORT=%s)\n", hbuf, sbuf);

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

void accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();

    st_d_error( "Got an error %d (%s) on the listener. "
                "Shutting down...\n", err, evutil_socket_error_to_string(err));
    event_base_loopexit(base, NULL);

    return;
}



#if 0
extern P_SESSION_OBJ session_search(struct rb_root *root, sd_id128_t mach_uuid)
{
    struct rb_node *node = root->rb_node;
    P_SESSION_OBJ p_sesson_obj = NULL;

    while (node)
    {
        p_sesson_obj = container_of(node, SESSION_OBJ, node);

        if (session_id < p_sesson_obj->session_id) 
            node = node->rb_left;
        else if (session_id > p_sesson_obj->session_id)
            node = node->rb_right;
        else
            return p_sesson_obj;
    }

    return NULL;
}


extern RET_T session_insert(struct rb_root *root, P_SESSION_OBJ data)
{
    struct rb_node **new = &(root->rb_node), *parent = NULL;

    /* Figure out where to put new node */
    while (*new)
    {
        P_SESSION_OBJ this = container_of(*new, SESSION_OBJ, node);

        parent = *new;
        if ( data->session_id < this->session_id ) 
            new = &((*new)->rb_left);
        else if ( data->session_id > this->session_id )
            new = &((*new)->rb_right);
        else
            return RET_NO;
    }

    /* Add new node and rebalance tree. */
    rb_link_node(&data->node, parent, new);
    rb_insert_color(&data->node, root);

    return RET_YES;
}


extern void session_erase(P_SESSION_OBJ p_session_obj, struct rb_root *tree)
{
    if (!p_session_obj || ! tree)
        return;

    return rb_erase(&p_session_obj->node, tree);
}

#endif



/**
 * 读取事件，主要进行数据转发 
 */
void bufferread_cb(struct bufferevent *bev, void *ptr)
{
    size_t n = 0;
    PKG_HEAD head;
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if ( evbuffer_remove(input, &head, HEAD_LEN) != HEAD_LEN)
    {
        st_d_print("Can not read HEAD_LEN(%d), drop it!", HEAD_LEN);
        return;
    }

    char *dat = malloc(head.dat_len);
    if (!dat)
    {
        st_d_error("Allocating %d error!", head.dat_len); 
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

    st_d_print("REVING %s", dat);

    ulong crc = crc32(0L, dat, head.dat_len);
    if (crc != head.crc) 
    {
        st_d_error("Recv data may broken: %lu-%lu", crc, head.crc); 
        return;
    }

    st_d_print("RECV:%s", (char*)dat);

    return;
}

