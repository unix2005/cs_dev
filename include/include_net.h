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
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
// 统一socket关闭接口（Windows 下 SOCKET 为 64 位，显式转回 SOCKET 避免 Win32 截断告警）
#define Q_SOCK_CLOSE(fd) closesocket((SOCKET)(fd))
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
#elif defined(Q_SYS_WINDOWS)
/* Windows：无 sys/epoll.h、sys/timerfd.h、sys/un.h。
   事件多路复用由 q_reactor 的 WSAPoll 后端承担（见 api/q_evsrc_poll_*），
   事件标志沿用与 Linux epoll 一致的便携取值，由 q_evsrc_poll_* 转换为 POLLIN/POLLOUT。 */
#ifndef EPOLLIN
#define EPOLLIN   0x1
#define EPOLLOUT  0x4
#define EPOLLERR  0x8
#define EPOLLHUP  0x10
#endif
/* Windows 的 send() 无 MSG_NOSIGNAL（也不产生 SIGPIPE），置 0 保持源码跨平台一致 */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
/* 字节序转换由 <winsock2.h> 提供（htons/ntohs 等），此处仅需 64 位版本兜底 */
#ifndef htobe64
#define htobe64(x) _byteswap_uint64(x)
#define be64toh(x) _byteswap_uint64(x)
#endif
/* Unix Domain Socket：Win10 1803+ 的 Winsock 已支持 AF_UNIX，但 mingw-w64 头尚未提供
   <afunix.h>/<sys/un.h>，此处按 Windows SOCKADDR_UN 布局补齐，供 l_2/q_ipc 复用同一份源码。
   注意：Windows 下 SOCKET 为 64 位句柄，q_ipc 用 int 承载存在 Win64 截断风险，
   故 Windows 产物仅保证"可编译可链接"，UDS 通信仍以 Linux 上的 netd/bizd 为准。 */
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif
struct sockaddr_un {
    unsigned short sun_family;
    char           sun_path[UNIX_PATH_MAX];
};
#endif

/* ---- Winsock 惰性初始化（Windows 专属）----
   POSIX 下 socket 族函数开箱可用；Windows 必须先 WSAStartup()，否则 socket() /
   inet_pton() / getaddrinfo() 一律以 WSANOTINITIALISED 失败。这里提供幂等的一次性
   初始化，并由 Q_SOCKET() 自动调用，免去在每个调用点手工初始化（与既有 Q_SOCK_CLOSE
   宏同属“抹平 winsock/posix 差异”的平台封装）。
   注：WSAStartup 自身引用计数，多线程并发重复调用无害；失败时返回 -1，下次可重试。 */
#ifdef Q_SYS_WINDOWS
static inline int Q_WINSOCK_INIT(void)
{
    static volatile long g_ws_inited = 0;
    if (!g_ws_inited)
    {
        WSADATA wd;
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0)
            return -1;
        g_ws_inited = 1;
    }
    return 0;
}
/* 创建套接字；失败返回 (q_fd_t)-1（Windows 下即 INVALID_SOCKET 的 q_fd_t 形态） */
#define Q_SOCKET(af, type, proto) \
    (Q_WINSOCK_INIT() == 0 ? (q_fd_t)socket((af), (type), (proto)) : (q_fd_t)-1)
#else
/* POSIX（Linux/macOS）：socket 族开箱可用，无需初始化。
   展开为 ((void)0) 而非 0 —— 调用点写的是 `Q_WINSOCK_INIT();` 语句形式，
   若展开成 `0;` 会被 gcc/clang 判为 "statement with no effect"（-Wunused-value，属 -Wall）。 */
#define Q_WINSOCK_INIT() ((void)0)
#define Q_SOCKET(af, type, proto) ((q_fd_t)socket((af), (type), (proto)))
#endif

/**
 * 场景：设置socket为非阻塞，这是网络模块最常用场景
 * 跨平台封装：Q_FD_SET_NONBLOCK(sock_fd, enable)
 * sock_fd：Linux/mac是int fd；Windows是SOCKET类型
 * enable: 1=开启非阻塞，0=关闭
 */
static inline int Q_FD_SET_NONBLOCK(q_fd_t sock_fd, int enable)
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

