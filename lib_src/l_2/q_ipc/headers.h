#ifndef Q_IPC_HEADERS_H
#define Q_IPC_HEADERS_H
/* 本库聚合公共基础头（统一走 include_*.h，禁止在 .c 直接 #include 系统头）。
   include_net.h 提供 socket/sys-un 等网络头 */
#include "include_stdio.h"
#include "include_time.h"
#include "include_net.h"
#endif /* Q_IPC_HEADERS_H */
