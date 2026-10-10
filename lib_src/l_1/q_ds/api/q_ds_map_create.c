/**
 * @file q_ds_map_create.c
 * @brief q_ds_map_create —— 创建空哈希表
 */
#include "headers.h"
#include "q_ds.h"

q_ds_map_t *q_ds_map_create(void)
{
    q_ds_map_t *m = (q_ds_map_t *)calloc(1, sizeof(q_ds_map_t));
    if (!m)
        return NULL;

    m->bucket_count = Q_DS_MAP_BUCKETS;
    m->buckets = (struct q_ds_map_entry **)calloc(m->bucket_count, sizeof(struct q_ds_map_entry *));
    if (!m->buckets)
    {
        free(m);
        return NULL;
    }
    return m;
}
