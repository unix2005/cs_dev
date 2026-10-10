/**
 * @file q_net_srv_conn_handler.c
 * @brief q_net_srv_conn_handler —— reactor 线程内：连接可读 -> 读满后派发到 Worker
 *
 * Reactor 仅负责把 socket 数据读出来（读到 EAGAIN 或对端关闭），不在此做业务处理，
 * 避免大包/重逻辑阻塞事件循环；读到的整段数据作为任务交给工作线程池。
 */
#include "headers.h"
#include "q_net.h"
#include "q_net_srv_int.h"

void q_net_srv_conn_handler(int fd, uint32_t events, void *ctx)
{
    q_net_srv_t *s = (q_net_srv_t *)ctx;
    (void)events;

    size_t cap = 65536, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return;

    for (;;)
    {
        if (len == cap)
        {
            size_t ncap = cap * 2;
            char *nb = (char *)realloc(buf, ncap);
            if (!nb)
            {
                free(buf);
                return;
            }
            buf = nb;
            cap = ncap;
        }
        ssize_t n = recv(fd, buf + len, cap - len, 0);
        if (n > 0)
        {
            len += (size_t)n;
            continue;
        }
        if (n == 0)             /* 对端关闭 */
        {
            q_reactor_del(s->reactor, fd);
            q_net_close(fd);
            break;
        }
        /* n < 0 */
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;              /* 本次可读数据已读完 */
        q_reactor_del(s->reactor, fd);
        q_net_close(fd);
        break;
    }

    if (len > 0)
    {
        struct q_net_srv_task *t = (struct q_net_srv_task *)malloc(sizeof(*t));
        if (t)
        {
            t->fd = fd;
            t->buf = buf;
            t->len = len;
            t->srv = s;
            if (q_tpool_dispatch(s->pool, q_net_srv_worker, t) != 0)
            {
                free(buf);
                free(t);
            }
            return;             /* buf 所有权已转移给任务 */
        }
    }
    free(buf);
}
