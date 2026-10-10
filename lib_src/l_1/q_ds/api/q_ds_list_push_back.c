/**
 * @file q_ds_list_push_back.c
 * @brief q_ds_list_push_back —— 尾部追加
 */
#include "headers.h"
#include "q_ds.h"

int q_ds_list_push_back(q_ds_list_t *l, void *data)
{
    if (!l)
        return -1;

    struct q_ds_list_node *n = (struct q_ds_list_node *)malloc(sizeof(*n));
    if (!n)
        return -1;

    n->data = data;
    n->prev = l->tail;
    n->next = NULL;

    if (l->tail)
        l->tail->next = n;
    else
        l->head = n;
    l->tail = n;
    l->size++;
    return 0;
}
