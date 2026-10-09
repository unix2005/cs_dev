/**
 * @file headers.h
 * @brief q_mem 模块统一聚合头文件（仅模块内 api/ 与 ut/ 源文件引用）
 * @note  本文件集中引入本库所需的公共基础头（include_*.h）。
 *        规则要求：api/ 下每个 .c 只允许 `#include "headers.h"` 与 `#include "q_mem.h"`，
 *        禁止直接写 `#include "include_*.h"`。
 */
#ifndef Q_MEM_HEADERS_H
#define Q_MEM_HEADERS_H

/* 统一启用 POSIX / BSD 声明（必须在引入任何系统/公共头之前定义）；
   _DEFAULT_SOURCE 用于暴露 explicit_bzero（安全清零原语） */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "include_stdio.h"

#endif /* Q_MEM_HEADERS_H */
