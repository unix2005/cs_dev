/**
 * @file q_net_srv_worker.c
 * @brief q_net_srv_worker —— worker 线程内：执行业务回调
 */
#include "headers.h"
#include "q_net.h"
#include "q_net_srv_int.h"

void q_net_srv_worker(void *arg)
{
    struct q_net_srv_task *t = (struct q_net_srv_task *)arg;
    if (t->srv->on_data)
        t->srv->on_data(t->fd, t->buf, t->len, t->srv->ctx);
    free(t->buf);
    free(t);
}
