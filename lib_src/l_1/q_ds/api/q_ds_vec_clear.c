/**
 * @file q_ds_vec_clear.c
 * @brief q_ds_vec_clear —— 清空（free_fn 可选，用于释放元素）
 */
#include "headers.h"
#include "q_ds.h"

void q_ds_vec_clear(q_ds_vec_t *v, void (*free_fn)(void *))
{
    if (!v)
        return;
    if (free_fn)
    {
        for (size_t i = 0; i < v->size; i++)
            free_fn(v->items[i]);
    }
    v->size = 0;
}
