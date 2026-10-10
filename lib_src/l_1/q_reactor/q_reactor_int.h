#ifndef Q_REACTOR_INT_H
#define Q_REACTOR_INT_H
/* reactor 内部结构体（不安装到 include/，仅供本模块 reactor_*.c 使用） */
#include "q_reactor.h"

struct q_reactor_ent
{
    int fd;
    uint32_t events;
    q_reactor_handler_t h;
    void *ctx;
};

struct q_reactor
{
    int epfd;
    struct q_reactor_ent **ents;  /* 指针数组，指针稳定（单独 malloc） */
    size_t n, cap;
};

#endif /* Q_REACTOR_INT_H */
