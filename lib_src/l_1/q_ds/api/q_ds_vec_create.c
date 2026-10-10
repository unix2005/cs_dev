/**
 * @file q_ds_vec_create.c
 * @brief q_ds_vec_create —— 创建动态数组（cap<=0 时取默认 8）
 */
#include "headers.h"
#include "q_ds.h"

q_ds_vec_t *q_ds_vec_create(size_t cap)
{
    if (cap == 0)
        cap = 8;

    q_ds_vec_t *v = (q_ds_vec_t *)calloc(1, sizeof(q_ds_vec_t));
    if (!v)
        return NULL;

    v->items = (void **)malloc(cap * sizeof(void *));
    if (!v->items)
    {
        free(v);
        return NULL;
    }
    v->cap = cap;
    v->size = 0;
    return v;
}
