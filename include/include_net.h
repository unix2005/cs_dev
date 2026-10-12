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

#include <stdint.h>
/* 跨平台“套接字/文件句柄”统一类型：
   POSIX 下 fd 即为 int；Windows 下 SOCKET 是 64 位无符号(UINT_PTR)，
   用 int 存储会截断高位，故 Windows 用 uint64_t 原样保存 SOCKET 值，避免 Win64 句柄截断。
   回调/注册接口的 fd 形参统一用 q_fd_t，保证把完整句柄传到 handler。 */
#if defined(Q_SYS_WINDOWS)
  typedef uint64_t q_fd_t;
#else
  typedef int      q_fd_t;
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
// Linux 特有：事件多路复用、POSIX 定时器、Unix 域套接字（网络/事件子系统依赖）
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#elif defined(Q_SYS_MACOS)
#include <sys/un.h>
#include <libkern/OSByteOrder.h>
#include <sys/event.h>
/* 便携事件标志：与 Linux <sys/epoll.h> 实际取值对齐（EPOLLIN=1 EPOLLOUT=4 EPOLLERR=8 EPOLLHUP=16），
   跨平台稳定；kqueue 的 EVFILT_READ/EVFILT_WRITE 与这些位无关，由 q_reactor 后端自行转换。 */
#ifndef EPOLLIN
#define EPOLLIN   0x1
#define EPOLLOUT  0x4
#define EPOLLERR  0x8
#define EPOLLHUP  0x10
#endif
#define htobe16(x) OSSwapHostToBigInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#endif

/**
 * 场景：设置socket为非阻塞，这是网络模块最常用场景
 * 跨平台封装：Q_FD_SET_NONBLOCK(sock_fd, enable)
 * sock_fd：Linux/mac是int fd；Windows是SOCKET类型
 * enable: 1=开启非阻塞，0=关闭
 */
static inline int Q_FD_SET_NONBLOCK(int sock_fd, int enable)
{
#if defined(Q_SYS_WINDOWS)
    u_long mode = enable ? 1 : 0;
    return ioctlsocket((SOCKET)sock_fd, FIONBIO, &mode);
#else
    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (flags == -1)
    {
        return -1;
    }
    if (enable)
    {
        flags |= O_NONBLOCK;
    }
    else
    {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(sock_fd, F_SETFL, flags);
#endif
}

#endif // INCLUDE_NET_H

