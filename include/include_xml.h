/**
 * @file include_xml.h
 * @brief XML配置解析 统一依赖头文件（预留扩展）
 * @platform Windows/Linux/macOS
 * @desc 统一管理xml解析库依赖，后续接入tinyxml2/libxml2无需改业务代码
 */
#ifndef INCLUDE_XML_H
#define INCLUDE_XML_H

// 全局统一平台宏
#if defined(_WIN32) || defined(_WIN64)
#define Q_SYS_WINDOWS
#elif defined(__linux__)
#define Q_SYS_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
#define Q_SYS_MACOS
#endif

// 依赖项目基础公共头文件
#include "include_stdio.h"
#include "include_time.h"
// cJSON（vendored，$(DEP_HOME)/cjson）：路径由 Makefile 的 SOFT_INCL 以 -I 提供
// （编译期必需；写在链接变量里无效）。三端统一为 DEP_HOME 依赖根，交叉编译可覆盖。
#include "cJSON.h"

// 预留第三方XML库引入入口
// 示例：#include <tinyxml2.h>
// 所有业务代码仅 #include "include_xml.h" 即可适配全平台
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>
#include <libxml2/libxml/xpath.h>
#include <libxml2/libxml/xmlstring.h>
// 注：原先此处还重复写过一次 <cjson/cJSON.h>（带目录前缀），
//     那是 Windows 侧 cjson shim 遗留：路径布局在 Linux/macOS 下不存在，
//     会让 q_xcfg / bizd / login 在任何非 Windows 环境直接编译失败。已删除。

#endif // INCLUDE_XML_H

