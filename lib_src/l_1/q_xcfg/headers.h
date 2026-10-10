/**
 * @file headers.h
 * @brief q_xcfg 模块统一聚合头文件（仅模块内 api/ 与 ut/ 源文件引用）
 * @note  集中引入本库所需的公共基础头（include_stdio.h / include_xml.h），
 *        并统一定义 feature 宏以启用 strdup/strcasecmp 等 POSIX/BSD 声明。
 *        规则要求：api/ 下每个 .c 只允许 `#include "headers.h"` 与 `#include "q_xcfg.h"`。
 */
#ifndef Q_XCFG_HEADERS_H
#define Q_XCFG_HEADERS_H

/* feature 宏必须在引入任何系统/公共头之前定义 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "include_stdio.h"
#include "include_xml.h"
#include "q_xcfg.h"
#include "api/q_xcfg_internal.h"

#endif /* Q_XCFG_HEADERS_H */
