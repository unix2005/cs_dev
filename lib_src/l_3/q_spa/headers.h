#ifndef L_3_Q_SPA_HEADERS_H
#define L_3_Q_SPA_HEADERS_H
#include "include_stdio.h"
#include "include_time.h"
/* 需要字节序转换（htons/ntohs 等）：统一走 include_net.h 的平台分支，
   避免 .c 直接 #include <arpa/inet.h>（Windows 无此头）。 */
#include "include_net.h"
#endif /* L_3_Q_SPA_HEADERS_H */
