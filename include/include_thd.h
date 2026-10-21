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
/* Windows 线程模型：本项目线程/互斥/条件变量统一使用 POSIX pthread API 书写
   （见 q_reactor 线程池、q_log 的 q_mutex_t lock）。MinGW / LLVM-MinGW 通道由
   winpthread 提供该 API（链接需 -lpthread，见 Makefile.inc 的 LIB_PTHREAD），
   故此处与 POSIX 侧保持一致，避免 q_mutex_t 与 pthread_mutex_* 语义错配。
   注：MSVC / ARM64EC 通道无 winpthread，届时需改走 C11 threads 或 Win32 原生
   同步原语，并同步调整 q_mutex_t 定义——不在本 MinGW 通道范围内。 */
#include <pthread.h>
typedef pthread_mutex_t q_mutex_t;
#else
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
typedef pthread_mutex_t q_mutex_t;
#endif

#endif // INCLUDE_THD_H

