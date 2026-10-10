/**
 * @file q_ds_map_size.c
 * @brief q_ds_map_size —— 当前条目数
 */
#include "headers.h"
#include "q_ds.h"

size_t q_ds_map_size(q_ds_map_t *m)
{
    return m ? m->size : 0;
}
