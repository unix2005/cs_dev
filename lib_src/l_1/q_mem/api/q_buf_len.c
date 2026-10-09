/**
 * @file q_buf_len.c
 * @brief q_buf_len —— 有效长度
 */
#include "headers.h"
#include "q_mem.h"

size_t q_buf_len(const q_buf_t *b)
{
    return b ? b->len : 0;
}
