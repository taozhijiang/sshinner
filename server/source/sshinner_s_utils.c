#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>

#include <assert.h>

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "sshinner_s.h"

extern RET_T json_fetch_and_copy(struct json_object *p_obj, const char* key, char* store, int max_len)
{
    json_object *p_store_obj = NULL;

    if (!p_obj || !key || !strlen(key) || !store)
        return RET_NO;

    if (json_object_object_get_ex(p_obj, key, &p_store_obj) &&
        json_object_get_string_len(p_store_obj))
    {
        strncpy(store, json_object_get_string(p_store_obj), max_len);
        return RET_YES;
    }

    return RET_NO;
}

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

        if (json_object_object_get_ex(p_class,"thread_num",&p_store_obj))
            p_opt->thread_num = json_object_get_int(p_store_obj); 
        else
            p_opt->thread_num = 5; /*default*/

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


extern void ss_ret_cmd_ok(struct bufferevent *bev,
                          sd_id128_t uuid, enum DIREC direct)
{
    CTL_HEAD ret_head;
    memset(&ret_head, 0, CTL_HEAD_LEN);
    ret_head.mach_uuid = uuid;
    ret_head.cmd = HD_CMD_OK;
    ret_head.direct = direct; 

    bufferevent_write(bev, &ret_head, CTL_HEAD_LEN);

    return;
}

extern void ss_ret_cmd_err(struct bufferevent *bev,
                           sd_id128_t uuid, enum DIREC direct)
{
    CTL_HEAD ret_head;
    memset(&ret_head, 0, CTL_HEAD_LEN);
    ret_head.mach_uuid = uuid;
    ret_head.cmd = HD_CMD_ERROR; 
    ret_head.direct = direct; 

    bufferevent_write(bev, &ret_head, CTL_HEAD_LEN);

    return;
}


/**
 * 没有消息负载的发送
 */
RET_T sc_send_head_cmd(struct bufferevent *bev, P_CTL_HEAD p_head,
                       int cmd, enum DIREC direct, unsigned long extra_param)
{
    CTL_HEAD head;
    memset(&head, 0, CTL_HEAD_LEN);

    if (bev == NULL) 
    {
        st_d_error("bev == NULL");
        return RET_NO;
    }

    head.direct = direct;
    head.cmd = cmd;
    head.extra_param = extra_param;
    head.mach_uuid = p_head->mach_uuid; 
    head.usrport = p_head->usrport;
    head.daemonport = p_head->daemonport;

    bufferevent_write(bev, &head, CTL_HEAD_LEN);

    return RET_YES;
}

#if 0

evutil_socket_t
ss_get_tcp_socket_for_host(const char *hostname, ev_uint16_t port)
{
    char port_buf[6];
    struct evutil_addrinfo hints;
    struct evutil_addrinfo *answer = NULL;
    int err;
    evutil_socket_t sock;

    /* Convert the port to decimal. */
    evutil_snprintf(port_buf, sizeof(port_buf), "%d", (int)port);

    /* Build the hints to tell getaddrinfo how to act. */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* v4 or v6 is fine. */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP; /* We want a TCP socket */
    /* Only return addresses we can use. */
    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;

    /* Look up the hostname. */
    err = evutil_getaddrinfo(hostname, port_buf, &hints, &answer);
    if (err != 0) {
          st_d_error( "Error while resolving '%s': %s",
                  hostname, evutil_gai_strerror(err));
          return -1;
    }

    /* If there was no error, we should have at least one answer. */
    assert(answer);
    /* Just use the first answer. */
    sock = socket(answer->ai_family,
                  answer->ai_socktype,
                  answer->ai_protocol);
    if (sock < 0)
        return -1;
    if (connect(sock, answer->ai_addr, answer->ai_addrlen)) {
        /* Note that we're doing a blocking connect in this function.
         * If this were nonblocking, we'd need to treat some errors
         * (like EINTR and EAGAIN) specially. */
        EVUTIL_CLOSESOCKET(sock);
        return -1;
    }

    return sock;
}
#endif

int ss_connect_srv(struct sockaddr_in* sin)
{
    int reuseaddr_on = 1;

    int sk_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sk_fd < 0)
        return -1;

    if (setsockopt(sk_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, 
		sizeof(reuseaddr_on)) == -1)
    {
        st_d_print("Reuse socket opt faile!\n");
        return -1;
    }
    if (connect(sk_fd, (struct sockaddr *)sin, sizeof(struct sockaddr_in))) 
    {
        st_d_error("Connect to server failed!\n");
        close(socket);
        return -1;
    }
    
    return sk_fd;
}
