/**
 * @file q_reactor_loop.c
 * @brief q_reactor_loop —— 持续运行直到 *stop 非 0
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_loop(q_reactor_t *r, volatile int *stop, int timeout_ms)
{
    if (!r || !stop)
        return -1;
    while (!*stop)
    {
        if (q_reactor_run(r, timeout_ms) != 0)
            return -1;
    }
    return 0;
}
