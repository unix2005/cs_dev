/**
 * @file q_reactor_run.c
 * @brief q_reactor_run —— 执行一轮事件循环（单次调度：poll + 分发）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_run(q_reactor_t *r, int timeout_ms)
{
    if (!r)
        return -1;
    return q_disp_run(r->disp, timeout_ms);
}
