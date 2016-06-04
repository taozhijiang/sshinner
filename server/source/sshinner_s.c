#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

#include <json-c/json.h>
#include <json-c/json_tokener.h>

#include "sshinner_s.h"

static RET_T json_fetch_and_copy(struct json_object *p_obj, const char* key, char* store, int max_len)
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


/**
 * 读取事件，主要进行数据转发 
 */
void bufferread_cb(struct bufferevent *bev, void *ptr)
{
    size_t n = 0;
    PKG_HEAD head;
    RET_T  ret;

    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    if ( evbuffer_remove(input, &head, HEAD_LEN) != HEAD_LEN)
    {
        st_d_print("Can not read HEAD_LEN(%d), drop it!", HEAD_LEN);
        return;
    }

    void *dat = malloc(head.dat_len);
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

    ulong crc = crc32(0L, dat, head.dat_len);
    if (crc != head.crc) 
    {
        st_d_error("Recv data may broken: %lu-%lu", crc, head.crc); 
        return;
    }

    if (head.type == 'C') 
    {
        ret = ss_handle_ctl(&head, (char *)dat);
    }
    else if (head.type == 'D')
    {
        //ret = ss_handle_dat(&head, dat);
    }
    else
    {
        SYS_ABORT("Error type: %c!", head.type); 
    }

    return;
}

extern SRV_OPT srvopt;
extern struct  event_base *base;

static RET_T ss_handle_ctl(P_PKG_HEAD p_head, char* dat)
{
    json_object *new_obj = json_tokener_parse(dat);
    json_object *p_store_obj = NULL;
    P_ACCT_ITEM p_acct_item = NULL;
    P_ACTIV_ITEM p_activ_item = NULL;

    char username   [128];
    unsigned long   userid;
    char uuid_s     [32];
    sd_id128_t      mach_uuid;
    
    if (!new_obj)
    {
        st_d_error("Json parse error: %s", dat);
        return RET_NO;
    }

    json_fetch_and_copy(new_obj, "username", username, sizeof(username));
    userid = json_object_get_int(json_object_object_get(new_obj,"userid"));

    // USR->DAEMON
    if (p_head->direct == 1) 
    {
        p_acct_item = ss_find_acct_item(&srvopt, username, userid);
        if (!p_acct_item)
        {
            st_d_print("Account %s:%lu not exist!", username, userid);
            return RET_NO;
        }

        json_fetch_and_copy(new_obj, "r_mach_uuid", uuid_s, sizeof(uuid_s));
        sd_id128_from_string(uuid_s, &mach_uuid);

        if (p_activ_item = ss_uuid_search(&srvopt.uuid_tree, mach_uuid))
        {
            st_d_print("%s", SD_ID128_CONST_STR(p_activ_item->mach_uuid));
        }

        // 查找到了ACTIV_ITEM，进行实际的传输关联
    }
    // DAEMON->USR
    else if (p_head->direct == 2) 
    {
        p_acct_item = ss_find_acct_item(&srvopt, username, userid);
        if (p_acct_item || ss_uuid_search(&srvopt.uuid_tree, p_head->mach_uuid)) 
        {
            st_d_print("ITEM %s:%lu Already exist!", username, userid);
            return RET_NO;
        }


        p_acct_item = (P_ACCT_ITEM)malloc(sizeof(ACCT_ITEM));
        if (!p_acct_item)
        {
            st_d_print("Malloc faile!");
            return RET_NO;
        }

        strncpy(p_acct_item->username, username, sizeof(p_acct_item->username));
        p_acct_item->userid = userid;
        slist_init(&p_acct_item->items);

        p_activ_item = (P_ACTIV_ITEM)malloc(sizeof(ACTIV_ITEM));
        if (!p_activ_item)
        {
            st_d_print("Malloc faile!");
            return RET_NO;
        }

        p_activ_item->base = base;
        p_activ_item->mach_uuid = p_head->mach_uuid;
        p_activ_item->bev_daemon = NULL;
        p_activ_item->bev_usr = NULL;

        slist_add(&p_acct_item->list, &srvopt.acct_items);
        slist_add(&p_activ_item->list, &p_acct_item->items);
        ss_uuid_insert(&srvopt.uuid_tree, p_activ_item);

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
    }
}
