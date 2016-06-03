#include <stdio.h>
#include <systemd/sd-id128.h> 

#include "sshinner_c.h"

#include <json-c/json.h>
#include <json-c/json_tokener.h>


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

extern RET_T load_settings_client(P_CLT_OPT p_opt)
{
    json_object *p_obj = NULL;
    json_object *p_class = NULL;
    json_object *p_store_obj = NULL;
    char *ptr = NULL;

    if (!p_opt)
        return RET_NO;

    if( ! (p_obj = json_object_from_file("settings.json")) )
        return RET_NO;

    if(json_object_object_get_ex(p_obj,"server",&p_class))
    {
        st_d_print("handling server configuration....");

        if (json_object_object_get_ex(p_class,"ipaddr",&p_store_obj))
        {
            ptr = json_object_get_string(p_store_obj);
            p_opt->srv.sin_family = AF_INET;
            p_opt->srv.sin_addr.s_addr = inet_addr(ptr);
        }

        if (json_object_object_get_ex(p_class,"port",&p_store_obj))
            p_opt->srv.sin_port = htons(json_object_get_int(p_store_obj)); 
    }

    if(json_object_object_get_ex(p_obj,"client",&p_class))
    {
        st_d_print("handling client configuration....");

        json_fetch_and_copy(p_class, "username", p_opt->username, 128); 

        if (json_object_object_get_ex(p_class,"userid",&p_store_obj))
            p_opt->userid = json_object_get_int64(p_store_obj); 

        if (json_object_object_get_ex(p_class,"port",&p_store_obj))
            p_opt->l_port = json_object_get_int(p_store_obj); 
    }

    if (p_opt->C_TYPE == C_DAEMON) 
    {
        if (json_object_object_get_ex(p_class,"f_port",&p_store_obj))
            p_opt->opt.daemon.f_port = json_object_get_int(p_store_obj); 
    }
    else
    {
        if (json_object_object_get_ex(p_class,"r_mach_id",&p_store_obj))
            ptr = json_object_get_string(p_store_obj); 

        sd_id128_from_string(ptr, &p_opt->opt.usr.r_mach_uuid); 
    }

    json_object_put(p_obj);
    return RET_YES;
}

extern void dump_clt_opts(P_CLT_OPT p_opt)
{
    if (!p_opt)
        return;

    st_d_print("WORK MODE: %s", p_opt->C_TYPE==C_DAEMON ?
               "C_DAEMON":"C_USR");

    st_d_print("");
    st_d_print("USRNAME:%s", p_opt->username); 
    st_d_print("USRID:%lu", p_opt->userid);
    st_d_print("HOSTNAME:%s", p_opt->hostname);
    st_d_print("HOSTMACHID:%s", SD_ID128_CONST_STR(p_opt->mach_uuid));
    st_d_print("LOCALPORT:%d", p_opt->l_port);
    
    st_d_print("");
    st_d_print("SERVERIP:%s", inet_ntoa(p_opt->srv.sin_addr));
    st_d_print("SERVERPORT:%d", ntohs(p_opt->srv.sin_port));

    st_d_print("");
    if (p_opt->C_TYPE == C_DAEMON) 
    {
        st_d_print("F_PORT:%d", p_opt->opt.daemon.f_port); 
    }
    else
    {
        st_d_print("ACC_MACHID:%s", SD_ID128_CONST_STR(p_opt->opt.usr.r_mach_uuid));
    }

    return;
}
