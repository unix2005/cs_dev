/**
 * @file q_ds_map_set.c
 * @brief q_ds_map_set —— 写入键值（已存在则覆盖值）
 */
#include "headers.h"
#include "q_ds.h"

int q_ds_map_set(q_ds_map_t *m, const char *key, void *value)
{
    if (!m || !key)
        return -1;

    unsigned long h = q_ds_hash_str(key) % m->bucket_count;
    for (struct q_ds_map_entry *e = m->buckets[h]; e; e = e->next)
    {
        if (strcmp(e->key, key) == 0)
        {
            e->value = value; /* 覆盖已有键 */
            return 0;
        }
    }

    struct q_ds_map_entry *n = (struct q_ds_map_entry *)malloc(sizeof(*n));
    if (!n)
        return -1;
    n->key = strdup(key);
    if (!n->key)
    {
        free(n);
        return -1;
    }
    n->value = value;
    n->next = m->buckets[h];
    m->buckets[h] = n;
    m->size++;
    return 0;
}
