/**
 * @file q_ds_map_clear.c
 * @brief q_ds_map_clear —— 清空所有条目（free_fn 可选，用于释放值）
 */
#include "headers.h"
#include "q_ds.h"

void q_ds_map_clear(q_ds_map_t *m, void (*free_fn)(void *))
{
    if (!m)
        return;

    for (size_t i = 0; i < m->bucket_count; i++)
    {
        struct q_ds_map_entry *e = m->buckets[i];
        while (e)
        {
            struct q_ds_map_entry *next = e->next;
            if (free_fn)
                free_fn(e->value);
            free(e->key);
            free(e);
            e = next;
        }
        m->buckets[i] = NULL;
    }
    m->size = 0;
}
