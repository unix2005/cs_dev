/**
 * @file q_ds_list_create.c
 * @brief q_ds_list_create —— 创建空双向链表
 */
#include "headers.h"
#include "q_ds.h"

q_ds_list_t *q_ds_list_create(void)
{
    return (q_ds_list_t *)calloc(1, sizeof(q_ds_list_t));
}
