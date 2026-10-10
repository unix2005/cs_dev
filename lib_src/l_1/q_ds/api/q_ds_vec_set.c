/**
 * @file q_ds_vec_set.c
 * @brief q_ds_vec_set —— 设置下标处的值（越界返回 -1）
 */
#include "headers.h"
#include "q_ds.h"

int q_ds_vec_set(q_ds_vec_t *v, size_t idx, void *data)
{
    if (!v || idx >= v->size)
        return -1;
    v->items[idx] = data;
    return 0;
}
