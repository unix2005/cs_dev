/**
 * @file q_ds_list_find.c
 * @brief q_ds_list_find —— 线性查找首个匹配节点
 */
#include "headers.h"
#include "q_ds.h"

void *q_ds_list_find(q_ds_list_t *l, const void *key, int (*cmp)(const void *a, const void *b))
{
    if (!l || !cmp)
        return NULL;

    for (struct q_ds_list_node *n = l->head; n; n = n->next)
    {
        if (cmp(n->data, key) == 0)
            return n->data;
    }
    return NULL;
}
