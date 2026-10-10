/**
 * @file q_sec_rate_limit.c
 * @brief q_sec_rate_limit —— 定长窗口限流计数器
 */
#include "headers.h"
#include "q_sec.h"

struct q_sec_rate_limit
{
    size_t   max;
    uint64_t window;
    uint64_t cur_start;
    size_t   cur_count;
};

q_sec_rate_limit_t *q_sec_rate_limit_create(size_t max, uint64_t window_sec)
{
    if (max == 0)
        return NULL;
    q_sec_rate_limit_t *r = (q_sec_rate_limit_t *)malloc(sizeof(*r));
    if (!r)
        return NULL;
    r->max = max;
    r->window = window_sec;
    r->cur_start = 0;
    r->cur_count = 0;
    return r;
}

void q_sec_rate_limit_destroy(q_sec_rate_limit_t *r)
{
    free(r);
}

int q_sec_rate_limit_hit(q_sec_rate_limit_t *r, uint64_t now_sec)
{
    if (!r)
        return -1;

    if (r->window == 0)
    {
        /* 不按时间重置，仅作总量上限 */
        if (r->cur_count >= r->max)
            return 1;
        r->cur_count++;
        return 0;
    }

    if (now_sec >= r->cur_start + r->window)
    {
        r->cur_start = now_sec;
        r->cur_count = 0;
    }
    if (r->cur_count >= r->max)
        return 1;
    r->cur_count++;
    return 0;
}
