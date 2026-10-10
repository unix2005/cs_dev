/**
 * @file q_ipc.h
 * @brief l_2 能力层 Unix Domain Socket 封装库（libqipc）对外头文件
 * @layer  lib_src/l_2/q_ipc  ->  产出 libqipc
 * @note   职责（AI_SKILL_RULE §3.1 l_2）：封装 UDS，用于 netd 与 bizd 进程间通信。
 *         仅做传输封装，不做任何加解密/鉴权（安全由上层 q_sec / q_chan 负责）。
 */
#ifndef L_2_Q_IPC_H
#define L_2_Q_IPC_H

#include "include_stdio.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* 连接到监听端（path 为 UDS 路径）；成功 0，失败 -1（*fd_out 有效时须调用方关闭） */
    int q_ipc_connect(const char *path, int *fd_out);

    /* 创建并监听 UDS（若路径已存在先 unlink，便于重启）；backlog<=0 时取默认 8；
       成功 0，失败 -1 */
    int q_ipc_listen(const char *path, int backlog, int *fd_out);

    /* 接受连接；成功 0，失败 -1（*fd_out 有效时须调用方关闭） */
    int q_ipc_accept(int listen_fd, int *fd_out);

    /* 发送：循环直到全部发送（MSG_NOSIGNAL，避免 SIGPIPE）；返回已发送字节数，出错 -1 */
    ssize_t q_ipc_send(int fd, const void *buf, size_t len);

    /* 接收：单次 recv，返回收到的字节数，0 表示对端关闭，<0 出错 */
    ssize_t q_ipc_recv(int fd, void *buf, size_t cap);

    /* 关闭 fd；path 非空时 unlink（仅监听端应在退出时传 path 清理） */
    void q_ipc_close(int fd, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* L_2_Q_IPC_H */
