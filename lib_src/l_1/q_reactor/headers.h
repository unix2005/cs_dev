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
#ifndef Q_SYS_WINDOWS
/* POSIX poll：通用分发核心用 poll/pipe 实现；Windows 无 <poll.h>，
   由 <winsock2.h> 提供 struct pollfd（include_net.h 已引入），
   q_evsrc_poll_* 事件源直接使用 WSAPoll，故此处跳过。 */
#include <poll.h>
#endif
#include <fcntl.h>
#endif /* Q_REACTOR_HEADERS_H */
