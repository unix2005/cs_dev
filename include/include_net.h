/**
 * @file include_net.h
 * @brief 跨平台 TCP/UDP/Socket 网络基础统一头文件
 * @platform Windows/Linux/macOS
 * @note 抹平 winsock 与 posix socket 差异，修复macOS编译报错
 */
#ifndef INCLUDE_NET_H
#define INCLUDE_NET_H

// 全局统一平台宏
#if defined(_WIN32) || defined(_WIN64)
#define Q_SYS_WINDOWS
#elif defined(__linux__)
#define Q_SYS_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
#define Q_SYS_MACOS
#endif

#ifdef Q_SYS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
// 统一socket关闭接口
#define Q_SOCK_CLOSE(fd) closesocket(fd)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
// 统一socket关闭接口
#define Q_SOCK_CLOSE(fd) close(fd)
#endif

// 修复macOS无endian.h问题，全平台字节序兼容
#if defined(Q_SYS_LINUX)
#include <endian.h>
#elif defined(Q_SYS_MACOS)
#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#endif

#endif // INCLUDE_NET_H

