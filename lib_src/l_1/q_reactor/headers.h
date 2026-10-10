#ifndef Q_REACTOR_HEADERS_H
#define Q_REACTOR_HEADERS_H
/* q_reactor 模块聚合公共基础头（统一走 include_*.h，禁止在 .c 直接 #include 系统头）。
   include_net.h 提供 epoll（仅 Linux 可用）与 socket；include_time.h 提供时间辅助；
   include_thd.h 提供 pthread（线程池依赖）。通用分发核心本身只用 POSIX poll/pipe，
   由下面补充的 <poll.h>/<fcntl.h>/<errno.h> 提供，不依赖 epoll。 */
#include "include_stdio.h"
#include "include_net.h"
#include "include_time.h"
#include "include_thd.h"
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#endif /* Q_REACTOR_HEADERS_H */
