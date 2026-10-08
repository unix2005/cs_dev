/**
 * @file include_time.h
 * @brief 跨平台 时间、时间戳、延时、时钟 统一头文件
 * @platform Windows/Linux/macOS
 */
#ifndef INCLUDE_TIME_H
#define INCLUDE_TIME_H

// 全局统一平台宏
#if defined(_WIN32) || defined(_WIN64)
#define Q_SYS_WINDOWS
#elif defined(__linux__)
#define Q_SYS_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
#define Q_SYS_MACOS
#endif

#include <time.h>

#ifdef Q_SYS_WINDOWS
#include <windows.h>
// Windows 兼容毫秒级时间戳
static inline long long q_get_ms_timestamp(void) 
{
	return GetTickCount64();
}
#else
#include <sys/time.h>
#include <unistd.h>
// Linux/Mac 毫秒级时间戳统一封装
static inline long long q_get_ms_timestamp(void) 
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
#endif

// 跨平台延时宏（全平台行为一致）
#ifdef Q_SYS_WINDOWS
#define Q_SLEEP_S(s)    Sleep((s) * 1000)
#define Q_SLEEP_MS(ms)  Sleep(ms)
#else
#define Q_SLEEP_S(s)    sleep(s)
#define Q_SLEEP_MS(ms)  usleep((ms) * 1000)
#endif

#endif // INCLUDE_TIME_H

