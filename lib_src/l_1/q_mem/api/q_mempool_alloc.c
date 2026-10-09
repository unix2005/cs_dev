/**
 * @file q_mempool_alloc.c
 * @brief q_mempool_alloc —— 从池分配一块
 */
#include "headers.h"
#include "q_mem.h"

void *q_mempool_alloc(q_mempool_t *mp)
{
    if (!mp || mp->free_top == 0)
        return NULL;
    return mp->free_list[--mp->free_top];
}
