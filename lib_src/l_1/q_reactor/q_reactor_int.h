#ifndef Q_REACTOR_INT_H
#define Q_REACTOR_INT_H
/* reactor 内部结构体（不安装到 include/，仅供本模块 reactor_*.c 使用）。
   改造后：reactor 仅是「通用分发核心 q_disp + epoll 事件源插件」的便捷封装，
   语义与改造前一致（fd 事件的 handler 在循环线程内联执行）。 */
#include "q_reactor.h"
#include "q_disp_int.h"
#include "q_evsrc_int.h"

struct q_reactor
{
    q_disp_t          *disp;   /* 通用任务分发核心（拥有循环驱动与唤醒机制） */
    q_evsrc_epoll_t   *ep;     /* 挂载到 disp 的 epoll 事件源插件（由 disp 负责销毁） */
};

#endif /* Q_REACTOR_INT_H */
