/**
 * @file q_mempool_clear.c
 * @brief q_mempool_clear —— 安全擦除池内所有已分配块
 */
#include "headers.h"
#include "q_mem.h"

void q_mempool_clear(q_mempool_t *mp)
{
    if (!mp || !mp->base)
        return;
    /* 已分配块 = 整片区域 - 空闲栈中的块；直接整片擦除最简单且无遗漏 */
    q_mem_zero(mp->base, mp->block_size * mp->block_count);
}
