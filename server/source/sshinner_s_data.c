#include "rbtree.h"
#include "sshinner_s.h"

extern P_ACCT_ITEM  ss_find_acct_item(P_SRV_OPT p_opt, 
                                      const char* username, unsigned long userid)
{
    if (!p_opt || !username || slist_empty(&p_opt->acct_items)) 
        return NULL;

    P_ACCT_ITEM p_acct_item = NULL;

    slist_for_each_entry(p_acct_item, &p_opt->acct_items, list)
    {
        if (!strncasecmp(username, p_acct_item->username, strlen(p_acct_item->username))
            && userid == p_acct_item->userid) 
        {
            return p_acct_item;
        }
    }

    return NULL;
}

extern P_ACTIV_ITEM ss_uuid_search(struct rb_root *root, sd_id128_t uuid)
{
    struct rb_node *node = root->rb_node;
    P_ACTIV_ITEM p_active_item = NULL;

    while (node)
    {
        p_active_item = container_of(node, ACTIV_ITEM, node);

        if (memcmp(&uuid, &p_active_item->mach_uuid, MACH_UUID_LEN) < 0) 
        {
            node = node->rb_left;
        }
        else if (memcmp(&uuid, &p_active_item->mach_uuid, MACH_UUID_LEN) > 0)
        {
            node = node->rb_right;
        }
        else
            return p_active_item;
    }

    return NULL;
}


extern RET_T ss_uuid_insert(struct rb_root *root, P_ACTIV_ITEM data)
{
    struct rb_node **new = &(root->rb_node), *parent = NULL;

    /* Figure out where to put new node */
    while (*new)
    {
        P_ACTIV_ITEM this = container_of(*new, ACTIV_ITEM, node);

        parent = *new;
        if (memcmp(&data->mach_uuid, &this->mach_uuid, MACH_UUID_LEN) < 0) 
            new = &((*new)->rb_left);
        else if (memcmp(&data->mach_uuid, &this->mach_uuid, MACH_UUID_LEN) > 0) 
            new = &((*new)->rb_right);
        else
            return RET_NO;
    }

    /* Add new node and rebalance tree. */
    rb_link_node(&data->node, parent, new);
    rb_insert_color(&data->node, root);

    return RET_YES;
}


extern void ss_uuid_erase(P_ACTIV_ITEM data, struct rb_root *tree)
{
    if (!data || ! tree)
        return;

    return rb_erase(&data->node, tree);
}

extern RET_T ss_activ_item_remove(P_SRV_OPT p_opt, P_ACTIV_ITEM p_item)
{
    P_ACCT_ITEM p_acct_item = NULL;
    P_ACTIV_ITEM p_activ_item = NULL;

    slist_for_each_entry(p_acct_item, &p_opt->acct_items, list)
    {
        slist_for_each_entry(p_activ_item, &p_acct_item->items, list)
        {
            if (p_activ_item == p_item) 
            {
                st_d_print("Free session for %s:%d with %s", p_acct_item->username, 
                           p_acct_item->userid, SD_ID128_CONST_STR(p_activ_item->mach_uuid)); 
                // free this block
                ss_uuid_erase(p_acct_item, &p_opt->uuid_tree);
                slist_remove(&p_activ_item->list, &p_acct_item->items); 
                free(p_activ_item); 
                goto out_1;
            }
        }
    }

    return RET_NO;

out_1:
    
    if (slist_empty(&p_acct_item->items)) 
    {
        ss_acct_remove(p_opt, p_acct_item);
    }

}


extern RET_T ss_acct_remove(P_SRV_OPT p_opt, P_ACCT_ITEM p_item)
{
    P_SLIST_HEAD p_pos = NULL;
    P_SLIST_HEAD p_n = NULL;
    P_ACCT_ITEM p_acct_item = NULL;


    if (!slist_empty(&p_item->items)) 
    {
        SYS_ABORT("Can not remove acct with active items!");
    }

    slist_for_each_safe(p_pos, p_n/*internel use*/, &p_opt->acct_items)
    {
        p_acct_item = list_entry(p_pos, ACCT_ITEM, list);
        if (p_acct_item == p_item) 
        {               
            st_d_print("Free Acct for %s:%d", p_item->username, p_item->userid); 
            // free this block
            slist_remove(&p_item->list, &p_opt->acct_items);
            free(p_acct_item);
            break;
        }
    }

    return RET_YES;
}


