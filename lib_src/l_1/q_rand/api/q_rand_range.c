/**
 * @file q_rand_range.c
 * @brief q_rand_range —— 闭区间 [min, max] 内随机整数
 */
#include "headers.h"
#include "q_rand.h"

int q_rand_range(int min, int max)
{
    if (max <= min)
        return min;

    uint32_t r = 0;
    q_rand_bytes(&r, sizeof(r));
    return min + (int)(r % (uint32_t)(max - min + 1));
}
