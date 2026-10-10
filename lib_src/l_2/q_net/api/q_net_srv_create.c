/**
 * @file q_net_srv_create.c
 * @brief q_net_srv_create —— 创建 Reactor + Worker 服务器
 */
#include "headers.h"
#include "q_net.h"
#include "q_net_srv_int.h"

q_net_srv_t *q_net_srv_create(int nworkers)
{
    q_net_srv_t *s = (q_net_srv_t *)malloc(sizeof(*s));
    if (!s)
        return NULL;
    s->reactor = q_reactor_create();
    if (!s->reactor)
    {
        free(s);
        return NULL;
    }
    s->pool = q_tpool_create(nworkers, 0);
    if (!s->pool)
    {
        q_reactor_destroy(s->reactor);
        free(s);
        return NULL;
    }
    s->stop = 0;
    s->listen_fd = -1;
    s->on_data = NULL;
    s->ctx = NULL;
    return s;
}
