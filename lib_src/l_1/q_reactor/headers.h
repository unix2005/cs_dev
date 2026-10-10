#ifndef Q_REACTOR_HEADERS_H
#define Q_REACTOR_HEADERS_H
/* q_reactor 模块聚合公共基础头（统一走 include_*.h，禁止在 .c 直接 #include 系统头）。
   include_net.h 提供 epoll（EPOLLIN/EPOLLOUT 等事件宏与 epoll 接口，仅 Linux 可用）；
   include_time.h 提供 timerfd 所需的 time 结构；include_thd.h 提供 pthread（线程池依赖）。 */
#include "include_stdio.h"
#include "include_net.h"
#include "include_time.h"
#include "include_thd.h"
#endif /* Q_REACTOR_HEADERS_H */
