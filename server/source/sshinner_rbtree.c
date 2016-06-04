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

