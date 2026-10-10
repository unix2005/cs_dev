/**
 * @file q_ds_vec_capacity.c
 * @brief q_ds_vec_capacity —— 当前容量
 */
#include "headers.h"
#include "q_ds.h"

size_t q_ds_vec_capacity(q_ds_vec_t *v)
{
    return v ? v->cap : 0;
}
