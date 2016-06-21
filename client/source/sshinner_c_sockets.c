#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <assert.h>

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "sshinner_c.h"


void ss5_bufferread_cb_enc(struct bufferevent *bev, void *ptr)
{
    ENC_FRAME from_f;
    ENC_FRAME to_f;

    P_PORTTRANS p_trans = (P_PORTTRANS)ptr; 

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if (bev == p_trans->local_bev && p_trans->srv_bev) 
    {
        st_d_print("加密转发数据包LOCAL->SRV");    // 解密

        // 加密
        from_f.len = bufferevent_read(p_trans->local_bev, from_f.dat, FRAME_SIZE); 
        if (from_f.len > 0)
        {
            encrypt(&p_trans->ctx_enc, &from_f, &to_f);
            bufferevent_write(p_trans->srv_bev, to_f.dat, to_f.len); 
        }        
        else
        {
            st_d_error("读取数据出错！");
        }
    }
    else if (bev == p_trans->srv_bev && p_trans->local_bev) 
    {
        st_d_print("加密转发数据包SRV->LOCAL");    // 解密

        // 解密
        from_f.len = bufferevent_read(p_trans->srv_bev, from_f.dat, FRAME_SIZE);
        if (from_f.len > 0)
        {
            decrypt(&p_trans->ctx_dec, &from_f, &to_f);
            bufferevent_write(p_trans->local_bev, to_f.dat, to_f.len);
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

void ss5_bufferevent_cb(struct bufferevent *bev, short events, void *ptr)
{
    P_PORTTRANS p_trans = (P_PORTTRANS)ptr; 
    struct event_base *base = bufferevent_get_base(bev);

    //只有使用bufferevent_socket_connect进行的连接才会得到CONNECTED的事件
    if (events & BEV_EVENT_CONNECTED) 
    {
        st_d_print("GOT BEV_EVENT_CONNECTED event! ");
    } 
    else if (events & BEV_EVENT_ERROR) 
    {
        st_d_error("GOT BEV_EVENT_ERROR event[%d]! ", p_trans->l_port);

        sc_free_trans(p_trans);
    } 
    else if (events & BEV_EVENT_EOF) 
    {
        st_d_error("GOT BEV_EVENT_EOF event[%d]! ", p_trans->l_port); 

        sc_free_trans(p_trans);
    }
    else if (events & BEV_EVENT_TIMEOUT) 
    {
        st_d_error("GOT BEV_EVENT_TIMEOUT event[%d]! ", p_trans->l_port);

        sc_free_trans(p_trans);
    } 

    return;
}

/**
 * 只会在DAEMON端调用 
 */
void ss5_accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    char buf[512];

    getnameinfo (address, socklen,
               hbuf, sizeof(hbuf),sbuf, sizeof(sbuf),
               NI_NUMERICHOST | NI_NUMERICSERV);

    st_print("SS5 WELCOME NEW CONNECT (HOST=%s, PORT=%s)\n", hbuf, sbuf);

    /** 读取代理参数*/
    // blocking socket here!
    memset(buf, 0, sizeof(buf));
    read(fd, buf, sizeof(buf)); //actually 1+1+1~255
    if (buf[0] != 0x05)
    {
        st_d_error("ONLY SUPPORT SS5: %x!", buf[0]);
        close(fd);
        return;
    }

    write(fd, "\x05\x00", 2);  // NO AUTHENTICATION REQUIRED

    // Fixed Head
    // VER CMD RSV ATYP
    memset(buf, 0, sizeof(buf));
    read(fd, buf, sizeof(buf));
    if (buf[0] != 0x05 || buf[1] != 0x01 /*CONNECT*/  || 
        (buf[3] != 0x01 /*IP v4*/ && buf[3] != 0x03 /*DOMAINNAME*/ ))
    {
        st_d_error("FIX Request head check error！");
        close(fd);
        return;
    }

    P_C_ITEM p_c = (P_C_ITEM)calloc(sizeof(C_ITEM), 1);
    if (!p_c)
    {
        st_d_error("申请内存[%d]失败！", sizeof(C_ITEM));
        close(fd);
        return;
    }

    P_PORTTRANS p_trans = sc_create_trans(atoi(sbuf)); 
    if (!p_trans)
    {
        st_d_error("本地无空闲TRANS!");
        close(fd);
        return;
    }

    p_trans->is_enc = 1;
    p_trans->l_port = atoi(sbuf);  //先占用

    P_THREAD_OBJ p_threadobj  = sc_get_threadobj(p_trans->l_port); 
    p_c->socket  = fd;
    p_c->arg.ptr = p_trans;
    memcpy(p_c->buf, buf, sizeof(p_c->buf));


    slist_add(&p_c->list, &p_threadobj->conn_queue);

    write(p_threadobj->notify_send_fd, "Q", 1);

    return;
}
