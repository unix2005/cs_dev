/**
 * @file headers.h
 * @brief q_util 模块统一聚合头文件（仅模块内 api/ 与 ut/ 源文件引用）
 * @note  启用 _POSIX_C_SOURCE 以暴露 localtime_r 等 POSIX 接口；
 *        集中引入公共基础头、对外头与内部表/辅助。
 *        规则要求：api/ 下每个 .c 只允许 `#include "headers.h"` 与 `#include "q_util.h"`。
 */
#ifndef Q_UTIL_HEADERS_H
#define Q_UTIL_HEADERS_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "include_stdio.h"
#include <time.h>
#include "q_util.h"
#include "api/q_util_internal.h"

#endif /* Q_UTIL_HEADERS_H */
