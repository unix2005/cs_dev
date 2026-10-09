/**
 * @file q_buf_destroy.c
 * @brief q_buf_destroy —— 擦除并释放
 */
#include "headers.h"
#include "q_mem.h"

void q_buf_destroy(q_buf_t *b)
{
    if (!b)
        return;
    if (b->data)
        q_mem_zero(b->data, b->cap);
    free(b->data);
    free(b);
}
