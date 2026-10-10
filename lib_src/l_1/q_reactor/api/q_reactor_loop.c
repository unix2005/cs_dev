/**
 * @file q_reactor_loop.c
 * @brief q_reactor_loop —— 持续运行直到 *stop 非 0
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_loop(q_reactor_t *r, volatile int *stop, int timeout_ms)
{
    if (!r)
        return -1;
    return q_disp_loop(r->disp, stop, timeout_ms);
}
