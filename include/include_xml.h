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

// 预留第三方XML库引入入口
// 示例：#include <tinyxml2.h>
// 所有业务代码仅 #include "include_xml.h" 即可适配全平台
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>
#include <libxml2/libxml/xpath.h>
#include <libxml2/libxml/xmlstring.h>
#include <cjson/cJSON.h>

#endif // INCLUDE_XML_H

