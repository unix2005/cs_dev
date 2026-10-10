/**
 * @file q_disp_loop.c
 * @brief q_disp_loop —— 持续运行直到内部停止标志或 *stop 非 0
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

int q_disp_loop(q_disp_t *d, volatile int *stop, int timeout_ms)
{
    if (!d)
        return -1;
    while (!d->stop && (!stop || !*stop))
    {
        if (q_disp_run(d, timeout_ms) != 0)
            return -1;
    }
    return 0;
}
