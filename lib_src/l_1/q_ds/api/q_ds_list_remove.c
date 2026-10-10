/**
 * @file q_ds_list_remove.c
 * @brief q_ds_list_remove —— 删除首个匹配节点（返回是否删除）
 */
#include "headers.h"
#include "q_ds.h"

int q_ds_list_remove(q_ds_list_t *l, const void *key, int (*cmp)(const void *a, const void *b))
{
    if (!l || !cmp)
        return 0;

    for (struct q_ds_list_node *n = l->head; n; n = n->next)
    {
        if (cmp(n->data, key) == 0)
        {
            if (n->prev)
                n->prev->next = n->next;
            else
                l->head = n->next;
            if (n->next)
                n->next->prev = n->prev;
            else
                l->tail = n->prev;
            free(n);
            l->size--;
            return 1;
        }
    }
    return 0;
}
