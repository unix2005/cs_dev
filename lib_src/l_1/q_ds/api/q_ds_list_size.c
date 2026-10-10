/**
 * @file q_ds_list_size.c
 * @brief q_ds_list_size —— 当前节点数
 */
#include "headers.h"
#include "q_ds.h"

size_t q_ds_list_size(q_ds_list_t *l)
{
    return l ? l->size : 0;
}
