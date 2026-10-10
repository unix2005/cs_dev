/**
 * @file q_ds_map_destroy.c
 * @brief q_ds_map_destroy —— 释放哈希表及所有条目
 */
#include "headers.h"
#include "q_ds.h"

void q_ds_map_destroy(q_ds_map_t *m)
{
    if (!m)
        return;
    q_ds_map_clear(m, NULL);
    free(m->buckets);
    free(m);
}
