/**
 * @file q_ds_vec_destroy.c
 * @brief q_ds_vec_destroy —— 释放数组（不释放元素本身）
 */
#include "headers.h"
#include "q_ds.h"

void q_ds_vec_destroy(q_ds_vec_t *v)
{
    if (!v)
        return;
    q_ds_vec_clear(v, NULL);
    free(v->items);
    free(v);
}
