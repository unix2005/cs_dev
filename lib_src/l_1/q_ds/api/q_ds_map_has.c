/**
 * @file q_ds_map_has.c
 * @brief q_ds_map_has —— 键是否存在
 */
#include "headers.h"
#include "q_ds.h"

int q_ds_map_has(q_ds_map_t *m, const char *key)
{
    return q_ds_map_get(m, key) != NULL ? 1 : 0;
}
