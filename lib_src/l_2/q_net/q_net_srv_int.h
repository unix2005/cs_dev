#ifndef Q_NET_SRV_INT_H
#define Q_NET_SRV_INT_H
/* q_net_srv 内部头（不安装到 include/，仅供本模块 srv_*.c 使用） */
#include "q_net.h"
#include "q_reactor.h"

struct q_net_srv_task
{
    int fd;
    void *buf;
    size_t len;
    q_net_srv_t *srv;
};

struct q_net_srv
{
    q_reactor_t *reactor;       /* 通用事件循环（来自 q_reactor） */
    q_tpool_t   *pool;          /* 有界工作线程池（来自 q_reactor） */
    volatile int stop;
    int listen_fd;
    void (*on_data)(int fd, void *buf, size_t len, void *ctx);
    void *ctx;
};

/* reactor 线程内执行 */
void q_net_srv_accept_handler(int fd, uint32_t events, void *ctx);
void q_net_srv_conn_handler(int fd, uint32_t events, void *ctx);
/* worker 线程内执行 */
void q_net_srv_worker(void *arg);

#endif /* Q_NET_SRV_INT_H */
