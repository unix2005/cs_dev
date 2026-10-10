/**
 * @file q_net.h
 * @brief l_2 能力层 网络库（libqnet）对外头文件
 * @layer  lib_src/l_2/q_net  ->  产出 libqnet
 * @note   职责（AI_SKILL_RULE §3.1 l_2）：socket 跨平台封装、UDP、客户端阻塞收发、
 *         地址解析、**Reactor + Worker 服务器**（Reactor 读取 socket 数据后派发到工作线程池处理）。
 *         通用事件循环与有界线程池复用 l_1/q_reactor（libqreactor），本层仅负责 socket 语义，
 *         **禁止引入 libuv**。
 *         加解密/协议逻辑不在此处（由 q_crypto / q_codec / q_chan 负责）。
 */
#ifndef L_2_Q_NET_H
#define L_2_Q_NET_H

#include "include_stdio.h"
#include <stdint.h>               /* 标准整数类型，公开头直接包含（无平台差异） */
#include "include_net.h"          /* 跨平台聚合：socket/epoll/netinet/arpa/inet 等 */

#ifdef __cplusplus
extern "C" {
#endif

    /* ===================== 基础 socket ===================== */
    int q_net_socket_tcp(int *fd_out);                 /* 创建 TCP socket */
    int q_net_set_nonblock(int fd);                   /* 设为非阻塞 */
    int q_net_close(int fd);                          /* 关闭 */
    int q_net_bind_listen(int port, int backlog, int *fd_out);   /* 绑定并监听 (INADDR_ANY) */
    int q_net_connect(const char *host, int port, int *fd_out); /* 连接 IPv4 */

    /* —— IPv6 / UDP 扩展 —— */
    int q_net_socket_udp(int *fd_out);                /* IPv4 UDP socket */
    int q_net_socket_tcp6(int *fd_out);               /* IPv6 TCP socket（v6-only） */
    int q_net_socket_udp6(int *fd_out);               /* IPv6 UDP socket（v6-only） */
    int q_net_bind_listen6(int port, int backlog, int *fd_out);  /* IPv6 TCP 绑定并监听(IN6ADDR_ANY) */
    int q_net_connect6(const char *host, int port, int *fd_out); /* 连接 IPv6 */
    int q_net_bind(int port, int *fd_out);            /* IPv4 UDP 绑定(INADDR_ANY，不 listen) */
    int q_net_bind6(int port, int *fd_out);           /* IPv6 UDP 绑定(IN6ADDR_ANY，不 listen) */
    int q_net_accept(int listen_fd, int *fd_out);     /* 接受连接（v4/v6 通用） */

    /* ===================== UDP 收发（v4/v6 通用，基于 sockaddr） ===================== */
    /* 发送到指定对端地址；to 为 sockaddr_in/sockaddr_in6，tolen 为其长度；
       to=NULL 且 tolen=0 时按已 connect 的对端发送；返回已发送字节数，出错 -1 */
    ssize_t q_net_udp_send(int fd, const void *buf, size_t len,
                           const struct sockaddr *to, socklen_t tolen);
    /* 接收并填对端地址；from 可为 NULL；返回字节数，出错 -1 */
    ssize_t q_net_udp_recv(int fd, void *buf, size_t cap,
                           struct sockaddr *from, socklen_t *fromlen);

    /* ===================== 地址辅助 ===================== */
    /* 解析 host:port 为 sockaddr_storage，自动识别 IPv4/IPv6（inet_pton）；成功 0，失败 -1 */
    int q_net_addr_resolve(const char *host, int port,
                           struct sockaddr_storage *ss, socklen_t *len);
    /* 生成通配绑定地址；v6!=0 用 IN6ADDR_ANY 否则 INADDR_ANY；成功 0，失败 -1 */
    int q_net_addr_any(int v6, int port, struct sockaddr_storage *ss, socklen_t *len);

    /* ===================== 客户端阻塞收发（TCP，循环至完成/EOR/错误） ===================== */
    /* 完整发送 len 字节（处理短写/EINTR）；返回已发送字节数，出错 -1 */
    ssize_t q_net_send_blocking(int fd, const void *buf, size_t len);
    /* 接收直到 len 字节或对端关闭；返回接收字节数（<len 表示 EOF），出错 -1 */
    ssize_t q_net_recv_blocking(int fd, void *buf, size_t len);

    /* ===================== Reactor + Worker 服务器（Reactor 读取，Worker 处理） ===================== */
    /* 模式：Reactor 在主线程读取 socket 数据（读到 EAGAIN / 对端关闭），再把整段数据作为任务
       派发到工作线程池，由 on_data 回调处理；对大包更安全，且不阻塞事件循环。
       底层复用通用 q_reactor（事件循环 + 有界线程池），本层仅负责 socket 语义。 */
    typedef struct q_net_srv q_net_srv_t;
    /* 业务处理回调：在 Worker 线程内执行；buf/len 为本次读取到的数据（由回调解读协议）；
       fd 为对端连接；ctx 为用户上下文。回调内可调用 q_net_send_blocking 回写响应。
       注意：连接生命周期由服务器管理，回调不应关闭 fd（关闭由对端 EOF 或服务器销毁处理）。 */
    typedef void (*q_net_srv_on_data)(int fd, void *buf, size_t len, void *ctx);

    q_net_srv_t *q_net_srv_create(int nworkers);            /* 创建 Reactor+Worker 服务器 */
    void q_net_srv_destroy(q_net_srv_t *s);                 /* 停止并销毁（join worker） */
    void q_net_srv_set_handler(q_net_srv_t *s, q_net_srv_on_data on_data, void *ctx);
    int  q_net_srv_listen(q_net_srv_t *s, int port, int backlog);    /* IPv4 监听并纳入 Reactor */
    int  q_net_srv_listen6(q_net_srv_t *s, int port, int backlog);   /* IPv6 监听并纳入 Reactor */
    void q_net_srv_run(q_net_srv_t *s);                     /* 运行 Reactor 循环（直到 q_net_srv_stop） */
    void q_net_srv_stop(q_net_srv_t *s);                    /* 请求停止循环 */

#ifdef __cplusplus
}
#endif

#endif /* L_2_Q_NET_H */
