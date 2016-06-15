#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <assert.h>

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "sshinner_c.h"


/**
 * 客户端和远程服务器的交互
 */
void srv_bufferread_cb(struct bufferevent *bev, void *ptr)
{
    size_t n = 0;
    CTL_HEAD head;

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if ( evbuffer_remove(input, &head, CTL_HEAD_LEN) != CTL_HEAD_LEN)
    {
        st_d_print("读取数据包头%d错误!", CTL_HEAD_LEN);
        return;
    }

    if (!sd_id128_equal(head.mach_uuid, cltopt.session_uuid))
    {
        SYS_ABORT("服务端返回UUID校验失败：%s-%s",
                  SD_ID128_CONST_STR(head.mach_uuid), SD_ID128_CONST_STR(cltopt.session_uuid)); 
    }

    if (head.cmd == HD_CMD_ERROR) 
    {
        st_d_error("SERVER RETURNED ERROR!");
        exit(EXIT_SUCCESS);
    }

    if (head.cmd == HD_CMD_CONN) 
    {
        assert(cltopt.C_TYPE == C_DAEMON);
        if (cltopt.C_TYPE == C_DAEMON) 
        {
            sc_find_daemon_portmap(head.daemonport, 1);
            P_PORTTRANS p_trans = NULL;
            int i = 0;
            for (i=0; i < MAX_PORT_NUM; ++i)
            {
                if (cltopt.trans[i].l_port == 0) 
                {
                    p_trans = &cltopt.trans[i];
                    break;
                }
            }
            
            if (!p_trans)
            {
                st_d_error("本地无空闲TRANS!");
                return;
            }

            /*建立本地连接*/
            int local_fd = socket(AF_INET, SOCK_STREAM, 0);
            int reuseaddr_on = 1;
            if (setsockopt(local_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, 
                sizeof(reuseaddr_on)) == -1)
            {
                st_d_error("Reuse socket opt faile!\n");
                return;
            }
            struct sockaddr_in  local_srv;
            local_srv.sin_family = AF_INET;
            local_srv.sin_addr.s_addr = inet_addr("127.0.0.1");
            local_srv.sin_port = htons(head.daemonport);

            if (connect(local_fd, (struct sockaddr *)&local_srv, sizeof(local_srv))) 
            {
                st_d_error("连接本地端口%d失败！", head.daemonport); 
                return;
            }
            else
            {
                st_d_print("连接本地端口%d OK！", head.daemonport); 
            }


            /*建立服务器连接*/
            int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
            if(sc_connect_srv(srv_fd) != RET_YES) 
            {
                st_d_error("连接服务器失败！");
                return;
            }


            struct event_base *base = bufferevent_get_base(bev);

            evutil_make_socket_nonblocking(local_fd);
            struct bufferevent *local_bev = 
                bufferevent_socket_new(base, local_fd, BEV_OPT_CLOSE_ON_FREE);
            bufferevent_setcb(local_bev, bufferread_cb, NULL, bufferevent_cb, p_trans);
            bufferevent_enable(local_bev, EV_READ|EV_WRITE);

            evutil_make_socket_nonblocking(srv_fd); 
            struct bufferevent *srv_bev = 
                bufferevent_socket_new(base, srv_fd, BEV_OPT_CLOSE_ON_FREE);
            bufferevent_setcb(srv_bev, bufferread_cb, NULL, bufferevent_cb, p_trans);
            bufferevent_enable(srv_bev, EV_READ|EV_WRITE);


            p_trans->daemonport = head.daemonport;
            p_trans->usrport = head.usrport;
            p_trans->l_port = head.extra_param;
            p_trans->local_bev = local_bev;
            p_trans->srv_bev = srv_bev;

            /* 向服务器报告连接请求 */
            //　必须要发送CONN包，触发这个连接转移到线程池处理  
                /* 向服务器报告连接请求 */
            CTL_HEAD ret_head;
            memset(&ret_head, 0, CTL_HEAD_LEN);
            ret_head.cmd = HD_CMD_CONN;
            ret_head.daemonport = p_trans->daemonport;
            ret_head.usrport = p_trans->usrport;
            ret_head.extra_param = p_trans->l_port; 
            ret_head.mach_uuid = cltopt.session_uuid;
            ret_head.direct = DAEMON_USR; 

            bufferevent_write(srv_bev, &ret_head, CTL_HEAD_LEN);

            st_d_print("DAEMON端准备OK!");
        }
    }

}


void srv_bufferevent_cb(struct bufferevent *bev, short events, void *ptr)
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
    }
    /*else if (events & BEV_EVENT_TIMEOUT) 
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
    */ 

    if (loop_terminate_flag)
    {
        bufferevent_free(bev);
        event_base_loopexit(base, NULL);
    }

    return;
}

/**
 * 客户端处理本地USR/DAEMON的请求
 */

void bufferevent_cb(struct bufferevent *bev, short events, void *ptr)
{
    struct event_base *base = bufferevent_get_base(bev);

    //只有使用bufferevent_socket_connect进行的连接才会得到CONNECTED的事件
    if (events & BEV_EVENT_CONNECTED) 
    {
        st_d_print("GOT BEV_EVENT_CONNECTED event! ");
    } 
    else if (events & BEV_EVENT_ERROR) 
    {
        st_d_print("GOT BEV_EVENT_ERROR event! ");

        bufferevent_free(bev);
        event_base_loopexit(base, NULL);
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

    return;
}


/**
 * 客户端程序，USR/DAEMON都是从应用程序读取到数据了，然后推送到SRV进行转发到服务器 
 */
void bufferread_cb(struct bufferevent *bev, void *ptr)
{
    P_PORTTRANS p_trans = (P_PORTTRANS)ptr; 

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if (bev == p_trans->local_bev && p_trans->srv_bev) 
    {
        bufferevent_write_buffer(p_trans->srv_bev, bufferevent_get_input(bev));
    }
    else if (bev == p_trans->srv_bev && p_trans->local_bev) 
    {
        bufferevent_write_buffer(p_trans->local_bev, bufferevent_get_input(bev));
    }
    else
    {
        SYS_ABORT("WRRRRRR!");
    }

    return;
}

/**
 * 只会在USR端被调用
 */
void accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    P_PORTMAP p_map = (P_PORTMAP)ctx; 
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

    getnameinfo (address, socklen,
               hbuf, sizeof(hbuf),sbuf, sizeof(sbuf),
               NI_NUMERICHOST | NI_NUMERICSERV);

    st_print("WELCOME NEW CONNECT (HOST=%s, PORT=%s)\n", hbuf, sbuf);

    /* We got a new connection! Set up a bufferevent for it. */
    struct event_base *base = evconnlistener_get_base(listener);

    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(sc_connect_srv(srv_fd) != RET_YES) 
    {
        st_d_error("连接服务器失败！");
        return;
    }

    P_PORTTRANS p_trans = NULL;

    int i = 0;
    for (i=0; i < MAX_PORT_NUM; ++i)
    {
        if (cltopt.trans[i].l_port == 0) 
        {
            p_trans = &cltopt.trans[i];
            break;
        }
    }

    if (!p_trans)
    {
        st_d_error("本地无空闲TRANS!");
        return;
    }

    struct bufferevent *local_bev = 
        bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(local_bev, bufferread_cb, NULL, bufferevent_cb, p_trans);
    bufferevent_enable(local_bev, EV_READ|EV_WRITE);

    struct bufferevent *srv_bev = 
        bufferevent_socket_new(base, srv_fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(srv_bev, bufferread_cb, NULL, bufferevent_cb, p_trans);
    bufferevent_enable(srv_bev, EV_READ|EV_WRITE);

    p_trans->usrport = p_map->usrport;
    p_trans->daemonport = p_map->daemonport;
    p_trans->local_bev = local_bev;
    p_trans->srv_bev = srv_bev;

    /* 向服务器报告连接请求 */
    CTL_HEAD ret_head;
    memset(&ret_head, 0, CTL_HEAD_LEN);
    ret_head.cmd = HD_CMD_CONN;
    ret_head.daemonport = p_map->daemonport;
    ret_head.usrport = p_map->usrport;
    ret_head.extra_param = atoi(sbuf);
    ret_head.mach_uuid = cltopt.session_uuid;
    ret_head.direct = USR_DAEMON;

    bufferevent_write(srv_bev, &ret_head, CTL_HEAD_LEN);

    st_d_print("客户端创建BEV OK！");

    /**
     * 有些服务是conn连接之后，服务端先发消息，然后客户端再进行响应的，所以 
     * 为了避免这种情况，客户端接收到conn消息之后，需要先向DAEMON端发送一个控制 
     * 消息，打通DAEMON端的数据传输接口 
     */

    return;
}

void accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();

    st_d_error( "Got an error %d (%s) on the listener. "
                "Shutting down...\n", err, evutil_socket_error_to_string(err));

    //event_base_loopexit(base, NULL);

    return;
}
