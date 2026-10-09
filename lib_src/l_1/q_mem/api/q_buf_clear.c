/**
 * @file q_buf_clear.c
 * @brief q_buf_clear —— 安全擦除内容（保留容量）
 */
#include "headers.h"
#include "q_mem.h"

void q_buf_clear(q_buf_t *b)
{
    if (!b || !b->data)
        return;
    q_mem_zero(b->data, b->cap);
    b->len = 0;
}
