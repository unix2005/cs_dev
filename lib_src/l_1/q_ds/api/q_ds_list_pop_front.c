/**
 * @file q_ds_list_pop_front.c
 * @brief q_ds_list_pop_front —— 头部弹出（返回数据，调用方负责释放）
 */
#include "headers.h"
#include "q_ds.h"

void *q_ds_list_pop_front(q_ds_list_t *l)
{
    if (!l || !l->head)
        return NULL;

    struct q_ds_list_node *n = l->head;
    void *data = n->data;

    l->head = n->next;
    if (l->head)
        l->head->prev = NULL;
    else
        l->tail = NULL;

    free(n);
    l->size--;
    return data;
}
