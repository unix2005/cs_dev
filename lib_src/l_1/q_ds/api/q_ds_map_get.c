/**
 * @file q_ds_map_get.c
 * @brief q_ds_map_get —— 按键取值（不存在返回 NULL）
 */
#include "headers.h"
#include "q_ds.h"

void *q_ds_map_get(q_ds_map_t *m, const char *key)
{
    if (!m || !key)
        return NULL;

    unsigned long h = q_ds_hash_str(key) % m->bucket_count;
    for (struct q_ds_map_entry *e = m->buckets[h]; e; e = e->next)
    {
        if (strcmp(e->key, key) == 0)
            return e->value;
    }
    return NULL;
}
