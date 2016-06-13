#include <errno.h>
#include <stdio.h>
#include <systemd/sd-id128.h> 

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
    PKG_HEAD ret_head;
    memset(&ret_head, 0, HEAD_LEN);
    ret_head.type = 'C';
    ret_head.mach_uuid = uuid;
    ret_head.ext = HD_EXT_OK;
    ret_head.direct = direct; 

    bufferevent_write(bev, &ret_head, HEAD_LEN);

    return;
}

extern void ss_ret_cmd_err(struct bufferevent *bev,
                           sd_id128_t uuid, enum DIREC direct)
{
    PKG_HEAD ret_head;
    memset(&ret_head, 0, HEAD_LEN);
    ret_head.type = 'C';
    ret_head.mach_uuid = uuid;
    ret_head.ext = HD_EXT_ERROR; 
    ret_head.direct = direct; 

    bufferevent_write(bev, &ret_head, HEAD_LEN);

    return;
}

extern void ss_ret_dat_err(struct bufferevent *bev,
                           sd_id128_t uuid, enum DIREC direct)
{
    PKG_HEAD ret_head;
    memset(&ret_head, 0, HEAD_LEN);
    ret_head.type = 'C';
    ret_head.mach_uuid = uuid;
    ret_head.ext = HD_EXT_DAT_ERROR; 
    ret_head.direct = direct; 

    bufferevent_write(bev, &ret_head, HEAD_LEN);

    return;
}


extern void ss_ret_cmd_keep(struct bufferevent *bev,
                            sd_id128_t uuid, enum DIREC direct)
{
    PKG_HEAD ret_head;
    memset(&ret_head, 0, HEAD_LEN);
    ret_head.type = 'C';
    ret_head.mach_uuid = uuid;
    ret_head.ext = HD_EXT_KEEP; 
    ret_head.direct = direct; 

    bufferevent_write(bev, &ret_head, HEAD_LEN);

    return;

}
