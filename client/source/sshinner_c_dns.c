#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <assert.h>

#include "sshinner_c.h"


void dns_client_to_proxy_cb(evutil_socket_t socket_fd, short ev_flags, void * ptr)
{

    P_PORTTRANS p_trans = (P_PORTTRANS)ptr; 
    ENC_FRAME from_f;
    ENC_FRAME to_f;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    socklen_t len; 

    int nread = recvfrom(socket_fd, (void *)from_f.dat, sizeof(from_f.dat), 
                         0, (struct sockaddr *)&sin, &len);

    if (nread > 0)
    {
        unsigned short trans_id = from_f.dat[0]<<8 | from_f.dat[1];
        cltopt.dns_transid_port_map[trans_id] = sin.sin_port;
        st_d_print("ADDING TRANSID-PORT: %u<->%u", trans_id, ntohs(sin.sin_port)); 

        from_f.len = nread;
        encrypt(&p_trans->ctx_enc, &from_f, &to_f);
        bufferevent_write(p_trans->srv_bev, to_f.dat, to_f.len); 
    }
}


void dns_bufferread_cb_enc(struct bufferevent *bev, void *ptr)
{
    ENC_FRAME from_f;
    ENC_FRAME to_f;

    P_PORTTRANS p_trans = (P_PORTTRANS)ptr; 

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if (bev == p_trans->srv_bev && !p_trans->local_bev) 
    {
        //st_d_print("加密转发数据包SRV->LOCAL");    // 解密

        // 解密
        from_f.len = bufferevent_read(p_trans->srv_bev, from_f.dat, FRAME_SIZE);
        if (from_f.len > 0)
        {
            decrypt(&p_trans->ctx_dec, &from_f, &to_f);   
            if (to_f.len > 0)
            {
                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));

                unsigned short trans_id = to_f.dat[0]<<8 | to_f.dat[1];
                sin.sin_addr.s_addr = inet_addr("127.0.0.1");
                sin.sin_port = cltopt.dns_transid_port_map[trans_id];
                st_d_print("RETRIVE TRANSID-PORT: %u<->%u", trans_id, ntohs(sin.sin_port)); 

                sendto(event_get_fd(p_trans->extra_ev), to_f.dat, to_f.len, 
                       0, (const struct sockaddr *)&sin, sizeof(sin));

            }
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

void dns_bufferevent_cb(struct bufferevent *bev, short events, void *ptr)
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
