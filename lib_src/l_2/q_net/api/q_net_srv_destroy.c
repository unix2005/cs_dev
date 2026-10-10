/**
 * @file q_net_srv_destroy.c
 * @brief q_net_srv_destroy —— 停止并销毁 Reactor + Worker 服务器
 */
#include "headers.h"
#include "q_net.h"
#include "q_net_srv_int.h"

void q_net_srv_destroy(q_net_srv_t *s)
{
    if (!s)
        return;
    s->stop = 1;
    if (s->listen_fd >= 0)
        q_net_close(s->listen_fd);
    /* 销毁顺序：先停 reactor 循环（epfd 关闭），再 join worker 池 */
    q_reactor_destroy(s->reactor);
    q_tpool_destroy(s->pool);
    free(s);
}
