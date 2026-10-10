/**
 * @file q_ds_list_clear.c
 * @brief q_ds_list_clear —— 清空所有节点（free_fn 可选，用于释放数据）
 */
#include "headers.h"
#include "q_ds.h"

void q_ds_list_clear(q_ds_list_t *l, void (*free_fn)(void *))
{
    if (!l)
        return;

    struct q_ds_list_node *n = l->head;
    while (n)
    {
        struct q_ds_list_node *next = n->next;
        if (free_fn)
            free_fn(n->data);
        free(n);
        n = next;
    }
    l->head = NULL;
    l->tail = NULL;
    l->size = 0;
}
