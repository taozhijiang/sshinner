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
