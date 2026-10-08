/**
 * @file include_stdio.h
 * @brief 跨平台 基础IO/文件/内存/字符串 统一聚合头文件
 * @platform Windows/Linux/macOS
 * @rule 项目所有IO、内存、字符串操作统一引用此文件
 */
#ifndef INCLUDE_STDIO_H
#define INCLUDE_STDIO_H

// 全局统一平台宏（同源对齐所有头文件）
#if defined(_WIN32) || defined(_WIN64)
#define Q_SYS_WINDOWS
#elif defined(__linux__)
#define Q_SYS_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
#define Q_SYS_MACOS
#endif

// 标准基础库
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

// 跨平台文件、IO兼容
#ifdef Q_SYS_WINDOWS
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
// Windows 兼容POSIX文件模式宏
#define S_IRUSR _S_IRUSR
#define S_IWUSR _S_IWUSR
#define S_IXUSR _S_IXUSR
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

// 通用内存、字符串工具兼容宏（全平台通用）
#define Q_MEM_ZERO(p, s)    memset(p, 0, s)
#define Q_STR_EMPTY(s)     ((s) == NULL || *(s) == '\0')
#define Q_MEM_COPY(d,s,n)   memcpy(d,s,n)

#endif // INCLUDE_STDIO_H

