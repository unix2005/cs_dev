/**
 * @file q_mempool_avail.c
 * @brief q_mempool_avail —— 空闲块数
 */
#include "headers.h"
#include "q_mem.h"

size_t q_mempool_avail(const q_mempool_t *mp)
{
    if (!mp)
        return 0;
    return mp->free_top;
}
