/**
 * @file q_disp_stop.c
 * @brief q_disp_stop —— 请求停止循环并唤醒阻塞中的 q_disp_run/loop
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

void q_disp_stop(q_disp_t *d)
{
    if (!d)
        return;
    d->stop = 1;
    q_disp_wake(d);
}
