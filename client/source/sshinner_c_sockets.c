#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <assert.h>

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "sshinner_c.h"


void bufferread_cb_enc(struct bufferevent *bev, void *ptr)
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
        return;
    }

    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(sc_connect_srv(srv_fd) != RET_YES) 
    {
        st_d_error("连接服务器失败！");
        return;
    }

    P_PORTTRANS p_trans = sc_create_trans(atoi(sbuf)); 
    if (!p_trans)
    {
        st_d_error("本地无空闲TRANS!");
        close(srv_fd);
        return;
    }
    p_trans->l_port = atoi(sbuf);  //先占用

    if (sc_daemon_ss5_init_srv(srv_fd, buf, p_trans->l_port) != RET_YES) 
    {
        p_trans->l_port = 0;
        close(srv_fd);
        st_d_error("服务器返回错误!");
        return;
    }

    /* We got a new connection! Set up a bufferevent for it. */
    struct event_base *base = evconnlistener_get_base(listener);

    p_trans->is_enc = 1;
    encrypt_ctx_init(&p_trans->ctx_enc, atoi(sbuf), cltopt.enc_key, 1); 
    encrypt_ctx_init(&p_trans->ctx_dec, atoi(sbuf), cltopt.enc_key, 0); 

    evutil_make_socket_nonblocking(fd);
    struct bufferevent *local_bev = 
        bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    assert(local_bev);
    bufferevent_setcb(local_bev, bufferread_cb_enc, NULL, bufferevent_cb, p_trans);
    //fferevent_enable(local_bev, EV_READ|EV_WRITE);

    evutil_make_socket_nonblocking(srv_fd);
    struct bufferevent *srv_bev = 
        bufferevent_socket_new(base, srv_fd, BEV_OPT_CLOSE_ON_FREE);
    assert(srv_bev);
    bufferevent_setcb(srv_bev, bufferread_cb_enc, NULL, bufferevent_cb, p_trans); 
    //fferevent_enable(srv_bev, EV_READ|EV_WRITE);

    st_d_print("DDDDD: 当前活动连接数：[[[ %d ]]]", 
               slist_count(&cltopt.trans)); 

    p_trans->local_bev = local_bev;
    p_trans->srv_bev = srv_bev;

    return;
}
