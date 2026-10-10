/**
 * @file q_ds_vec_push.c
 * @brief q_ds_vec_push —— 尾部追加（容量不足自动翻倍）
 */
#include "headers.h"
#include "q_ds.h"

int q_ds_vec_push(q_ds_vec_t *v, void *data)
{
    if (!v)
        return -1;

    if (v->size == v->cap)
    {
        size_t ncap = v->cap * 2;
        void **ni = (void **)realloc(v->items, ncap * sizeof(void *));
        if (!ni)
            return -1;
        v->items = ni;
        v->cap = ncap;
    }
    v->items[v->size++] = data;
    return 0;
}
