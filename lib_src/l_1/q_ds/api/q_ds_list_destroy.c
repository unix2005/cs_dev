/**
 * @file q_ds_list_destroy.c
 * @brief q_ds_list_destroy —— 释放链表及所有节点
 */
#include "headers.h"
#include "q_ds.h"

void q_ds_list_destroy(q_ds_list_t *l)
{
    if (!l)
        return;
    q_ds_list_clear(l, NULL);
    free(l);
}
