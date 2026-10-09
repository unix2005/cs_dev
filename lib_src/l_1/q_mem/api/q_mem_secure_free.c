/**
 * @file q_mem_secure_free.c
 * @brief q_mem_secure_free —— 安全擦除后释放
 */
#include "headers.h"
#include "q_mem.h"

void q_mem_secure_free(void *p, size_t n)
{
    if (!p)
        return;
    q_mem_zero(p, n);
    free(p);
}
