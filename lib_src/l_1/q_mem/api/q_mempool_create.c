/**
 * @file q_mempool_create.c
 * @brief q_mempool_create —— 创建固定块内存池
 */
#include "headers.h"
#include "q_mem.h"

#define Q_MEMPOOL_ALIGN 16u

static size_t align_up(size_t v, size_t a)
{
    return (v + (a - 1)) & ~(size_t)(a - 1);
}

q_mempool_t *q_mempool_create(size_t block_size, size_t block_count)
{
    if (block_size == 0 || block_count == 0)
        return NULL;

    size_t bs = align_up(block_size, Q_MEMPOOL_ALIGN);
    size_t region = bs * block_count;

    /* 乘法溢出保护 */
    if (bs != 0 && region / bs != block_count)
        return NULL;

    q_mempool_t *mp = (q_mempool_t *)calloc(1, sizeof(q_mempool_t));
    if (!mp)
        return NULL;

    mp->base = (unsigned char *)malloc(region);
    if (!mp->base)
    {
        free(mp);
        return NULL;
    }

    mp->free_list = (void **)malloc(sizeof(void *) * block_count);
    if (!mp->free_list)
    {
        free(mp->base);
        free(mp);
        return NULL;
    }

    mp->block_size = bs;
    mp->block_count = block_count;
    mp->free_top = block_count;

    /* 初始所有块均空闲，栈顶指向末尾块 */
    size_t i;
    for (i = 0; i < block_count; i++)
        mp->free_list[i] = mp->base + i * bs;

    return mp;
}
