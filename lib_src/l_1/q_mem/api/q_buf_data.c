/**
 * @file q_buf_data.c
 * @brief q_buf_data —— 只读数据指针
 */
#include "headers.h"
#include "q_mem.h"

const void *q_buf_data(const q_buf_t *b)
{
    if (!b || b->len == 0)
        return NULL;
    return b->data;
}
