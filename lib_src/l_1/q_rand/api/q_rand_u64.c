/**
 * @file q_rand_u64.c
 * @brief q_rand_u64 —— 随机 64 位无符号整数
 */
#include "headers.h"
#include "q_rand.h"

uint64_t q_rand_u64(void)
{
    uint64_t v = 0;
    q_rand_bytes(&v, sizeof(v));
    return v;
}
