/**
 * @file q_net_srv_listen6.c
 * @brief q_net_srv_listen6 —— IPv6 监听并纳入 Reactor
 */
#include "headers.h"
#include "q_net.h"
#include "q_net_srv_int.h"

int q_net_srv_listen6(q_net_srv_t *s, int port, int backlog)
{
    if (!s)
        return -1;
    int lf = -1;
    if (q_net_bind_listen6(port, backlog, &lf) != 0)
        return -1;
    if (q_reactor_add(s->reactor, lf, EPOLLIN, q_net_srv_accept_handler, s) != 0)
    {
        q_net_close(lf);
        return -1;
    }
    s->listen_fd = lf;
    return 0;
}
