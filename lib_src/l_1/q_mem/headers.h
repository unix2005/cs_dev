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

/* Windows：q_mem_zero() 使用 SecureZeroMemory（<winbase.h> 宏，映射到 RtlSecureZeroMemory）。
   项目规则禁止 api 目录下的 .c 直接引用系统头，故按平台在聚合头内引入。
   （仅本模块需要，不放入公共 include_stdio.h，避免 windows.h 宏污染全工程） */
#ifdef Q_SYS_WINDOWS
#include <windows.h>
#endif

#endif /* Q_MEM_HEADERS_H */
