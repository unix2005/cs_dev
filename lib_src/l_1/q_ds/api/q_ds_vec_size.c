/**
 * @file q_ds_vec_size.c
 * @brief q_ds_vec_size —— 当前元素数
 */
#include "headers.h"
#include "q_ds.h"

size_t q_ds_vec_size(q_ds_vec_t *v)
{
    return v ? v->size : 0;
}
