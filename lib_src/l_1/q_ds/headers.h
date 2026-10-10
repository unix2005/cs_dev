/**
 * @file headers.h
 * @brief q_ds 模块统一聚合头文件（仅模块内 api/ 与 ut/ 源文件引用）
 * @note  集中引入本库所需的公共基础头（include_stdio.h）与对外头（q_ds.h）、
 *        内部结构与辅助（api/q_ds_internal.h）。
 *        规则要求：api/ 下每个 .c 只允许 `#include "headers.h"` 与 `#include "q_ds.h"`。
 */
#ifndef Q_DS_HEADERS_H
#define Q_DS_HEADERS_H

/* 确保 strdup 等 POSIX 接口可见（glibc 默认亦暴露，这里显式声明以防万一） */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "include_stdio.h"
#include "q_ds.h"
#include "api/q_ds_internal.h"

#endif /* Q_DS_HEADERS_H */
