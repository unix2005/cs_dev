/**
 * @file headers.h
 * @brief q_log 模块统一聚合头文件（仅模块内 api/ 与 ut/ 源文件引用）
 * @note  本文件集中引入本库所需的全部公共基础头（include_*.h），
 *        并统一定义 _POSIX_C_SOURCE 以启用 fdopen/open/localtime_r 等 POSIX 声明。
 *        规则要求：api/ 下每个 .c 只允许 `#include "headers.h"` 与 `#include "q_log.h"`，
 *        禁止直接写 `#include "include_*.h"`。
 */
#ifndef Q_LOG_HEADERS_H
#define Q_LOG_HEADERS_H

/* 统一启用 POSIX 声明（必须在引入任何系统/公共头之前定义） */
#define _POSIX_C_SOURCE 200809L

#include "include_stdio.h"
#include "include_time.h"
#include "include_thd.h"

#endif /* Q_LOG_HEADERS_H */
