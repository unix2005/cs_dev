/**
 * @file q_ds_list_push_front.c
 * @brief q_ds_list_push_front —— 头部插入
 */
#include "headers.h"
#include "q_ds.h"

int q_ds_list_push_front(q_ds_list_t *l, void *data)
{
    if (!l)
        return -1;

    struct q_ds_list_node *n = (struct q_ds_list_node *)malloc(sizeof(*n));
    if (!n)
        return -1;

    n->data = data;
    n->prev = NULL;
    n->next = l->head;

    if (l->head)
        l->head->prev = n;
    else
        l->tail = n;
    l->head = n;
    l->size++;
    return 0;
}
