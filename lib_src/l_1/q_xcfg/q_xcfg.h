/**
 * @file q_xcfg.h
 * @brief l_1 基础层 内存库（libqxcfg）对外头文件
 * @platform Windows/Linux/macOS
 * @layer  lib_src/l_1/q_xcfg  → 产出 libqxcfg
 * @note   本文件自包含：直接引入所需公共基础头（include_stdio.h），
 *         故外部模块 `#include <q_xcfg.h>` 可独立编译，无需依赖内部聚合头 headers.h。
 *         api/ 下源文件仍只需 `#include "headers.h"` + `#include "q_xcfg.h"`。
 *
 * 职责（依据 AI_SKILL_RULE §3.1 l_1 基础层）：
 *   - 配置读取接口:内部借助 libxml2 的 XPath 引擎,
 *     将"点分键"（如 service.port）转换为 XPath 表达式（/service/port/text()）
 *     后求值。所有公开函数对 NULL 上下文/键均做防御性处理并返回默认值或 NULL.
 *
 */

#ifndef L_1_Q_XCFG_H
#define L_1_Q_XCFG_H

/* 本文件自包含：直接引入所需公共基础头，便于外部模块独立编译。
   （feature 宏 _POSIX_C_SOURCE/_DEFAULT_SOURCE 由 api/ 源文件的 headers.h 统一定义，
    本公共头不重复定义，避免外部 TU 重复包含时触发 redefine 告警） */
#include "include_stdio.h"
#include "include_xml.h"

#ifdef __cplusplus
extern "C"
{
#endif

	struct config_ctx
	{
		xmlDocPtr doc;           // XML 文档指针
		xmlXPathContextPtr xpath; // XPath 上下文
		char *error_msg;         // 错误信息
	};


#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_MEM_H */
