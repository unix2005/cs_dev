/**
 * @file q_ds_vec_pop.c
 * @brief q_ds_vec_pop —— 尾部弹出（空则返回 NULL）
 */
#include "headers.h"
#include "q_ds.h"

void *q_ds_vec_pop(q_ds_vec_t *v)
{
    if (!v || v->size == 0)
        return NULL;
    return v->items[--v->size];
}
