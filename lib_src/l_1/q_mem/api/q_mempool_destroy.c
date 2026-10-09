/**
 * @file q_mempool_destroy.c
 * @brief q_mempool_destroy —— 擦除并释放池
 */
#include "headers.h"
#include "q_mem.h"

void q_mempool_destroy(q_mempool_t *mp)
{
    if (!mp)
        return;
    if (mp->base)
        q_mem_zero(mp->base, mp->block_size * mp->block_count);
    free(mp->base);
    free(mp->free_list);
    free(mp);
}
