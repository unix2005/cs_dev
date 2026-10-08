/**
 * @file include_thd.h
 * @brief 跨平台 线程、进程、互斥锁、同步 统一头文件
 * @platform Windows/Linux/macOS
 */
#ifndef INCLUDE_THD_H
#define INCLUDE_THD_H

// 全局统一平台宏
#if defined(_WIN32) || defined(_WIN64)
#define Q_SYS_WINDOWS
#elif defined(__linux__)
#define Q_SYS_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
#define Q_SYS_MACOS
#endif

#ifdef Q_SYS_WINDOWS
#include <windows.h>
#include <process.h>
// 兼容类POSIX锁类型定义
typedef CRITICAL_SECTION q_mutex_t;
#else
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
typedef pthread_mutex_t q_mutex_t;
#endif

#endif // INCLUDE_THD_H

