/**
 * @file q_ds_vec_get.c
 * @brief q_ds_vec_get —— 按下标取值（越界返回 NULL）
 */
#include "headers.h"
#include "q_ds.h"

void *q_ds_vec_get(q_ds_vec_t *v, size_t idx)
{
    if (!v || idx >= v->size)
        return NULL;
    return v->items[idx];
}
