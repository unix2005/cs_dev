/**
 * @file q_rand_u32.c
 * @brief q_rand_u32 —— 随机 32 位无符号整数
 */
#include "headers.h"
#include "q_rand.h"

uint32_t q_rand_u32(void)
{
    uint32_t v = 0;
    q_rand_bytes(&v, sizeof(v));
    return v;
}
