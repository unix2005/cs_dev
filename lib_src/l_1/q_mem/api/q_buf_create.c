/**
 * @file q_buf_create.c
 * @brief q_buf_create —— 创建动态缓冲区
 */
#include "headers.h"
#include "q_mem.h"

q_buf_t *q_buf_create(size_t cap)
{
    q_buf_t *b = (q_buf_t *)calloc(1, sizeof(q_buf_t));
    if (!b)
        return NULL;

    if (cap == 0)
        cap = 64; /* 懒惰分配下限，避免每次 append 都 malloc 0 */

    b->data = (unsigned char *)malloc(cap);
    if (!b->data)
    {
        free(b);
        return NULL;
    }
    b->cap = cap;
    b->len = 0;
    return b;
}
