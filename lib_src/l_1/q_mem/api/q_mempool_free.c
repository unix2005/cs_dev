/**
 * @file q_mempool_free.c
 * @brief q_mempool_free —— 归还一块到池（校验归属与重复归还）
 */
#include "headers.h"
#include "q_mem.h"

void q_mempool_free(q_mempool_t *mp, void *p)
{
    if (!mp || !p)
        return;

    unsigned char *up = (unsigned char *)p;

    /* 必须落在池区域且按块对齐，否则视为非法指针，安全忽略 */
    if (up < mp->base || up >= mp->base + mp->block_size * mp->block_count)
        return;
    if (((size_t)(up - mp->base)) % mp->block_size != 0)
        return;
    if (mp->free_top >= mp->block_count)
        return; /* 栈满，理论不应发生 */

    /* 拒绝重复归还（防 free 栈损坏） */
    size_t i;
    for (i = 0; i < mp->free_top; i++)
    {
        if (mp->free_list[i] == p)
            return;
    }

    mp->free_list[mp->free_top++] = p;
}
