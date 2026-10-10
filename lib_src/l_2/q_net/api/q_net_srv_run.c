/**
 * @file q_net_srv_run.c
 * @brief q_net_srv_run / q_net_srv_stop —— Reactor 循环启停
 */
#include "headers.h"
#include "q_net.h"
#include "q_net_srv_int.h"

void q_net_srv_run(q_net_srv_t *s)
{
    if (!s)
        return;
    q_reactor_loop(s->reactor, &s->stop, 200);
}

void q_net_srv_stop(q_net_srv_t *s)
{
    if (s)
        s->stop = 1;
}
