/**
 * @file headers.h
 * @brief q_rand 模块统一聚合头文件（仅模块内 api/ 与 ut/ 源文件引用）
 * @note  集中引入公共基础头（提供 open/read/close 等）、errno 与对外头。
 *        规则要求：api/ 下每个 .c 只允许 `#include "headers.h"` 与 `#include "q_rand.h"`。
 */
#ifndef Q_RAND_HEADERS_H
#define Q_RAND_HEADERS_H

#include "include_stdio.h"
#include <errno.h>
#include "q_rand.h"

#endif /* Q_RAND_HEADERS_H */
