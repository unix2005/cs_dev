/**
 * @file q_ds_map_del.c
 * @brief q_ds_map_del —— 删除键（返回是否删除）
 */
#include "headers.h"
#include "q_ds.h"

int q_ds_map_del(q_ds_map_t *m, const char *key)
{
    if (!m || !key)
        return 0;

    unsigned long h = q_ds_hash_str(key) % m->bucket_count;
    struct q_ds_map_entry *prev = NULL;
    for (struct q_ds_map_entry *e = m->buckets[h]; e; e = e->next)
    {
        if (strcmp(e->key, key) == 0)
        {
            if (prev)
                prev->next = e->next;
            else
                m->buckets[h] = e->next;
            free(e->key);
            free(e);
            m->size--;
            return 1;
        }
        prev = e;
    }
    return 0;
}
