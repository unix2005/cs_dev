/**
 * @file q_net_srv_accept_handler.c
 * @brief q_net_srv_accept_handler —— reactor 线程内：监听 fd 可读 -> accept 并注册连接
 */
#include "headers.h"
#include "q_net.h"
#include "q_net_srv_int.h"

void q_net_srv_accept_handler(int fd, uint32_t events, void *ctx)
{
    q_net_srv_t *s = (q_net_srv_t *)ctx;
    (void)events;
    /* 一次性 accept 所有就绪连接（ET/水平均可，循环直到 EAGAIN） */
    for (;;)
    {
        int cfd = -1;
        if (q_net_accept(fd, &cfd) != 0)
            break;              /* 无更多连接（EAGAIN 或出错） */
        if (q_net_set_nonblock(cfd) != 0)
        {
            q_net_close(cfd);
            continue;
        }
        if (q_reactor_add(s->reactor, cfd, EPOLLIN, q_net_srv_conn_handler, s) != 0)
            q_net_close(cfd);
    }
}
