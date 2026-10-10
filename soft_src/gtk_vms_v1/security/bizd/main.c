/*
 * bizd —— 业务进程（仅 Unix Socket，不直接暴露公网）
 *
 * 职责：业务逻辑、权限校验、审计日志（SM3 链式哈希）、视频流调度；
 *      持有 DB 凭据。入站数据均来自 netd 经 q_ipc 转发的可信 TLV，
 *      但仍需按 q_codec 字段校验表做结构校验，绝不信任原始字节。
 *
 * 骨架：主循环 + 降权占位（逻辑与公共件见 ../../common）。
 */
#include <stdio.h>
#include <unistd.h>
#include "daemon_signal.h"
#include "daemon_harden.h"

int main(void)
{
    if (daemon_install_signal_handlers() != 0) {
        fprintf(stderr, "[bizd] 信号处理器安装失败\n");
        return 1;
    }

    printf("[bizd] skeleton start (pid=%d)\n", (int)getpid());

    static const struct daemon_harden_cfg cfg = {
        .chroot_dir      = NULL, /* bizd 需访问 DB，不做 chroot */
        .drop_uid        = -1,   /* TODO: 改为专用非特权用户 bizd 的 uid */
        .seccomp_profile = 2,    /* bizd：收口 DB / Unix-Socket / 文件读写 */
    };
    daemon_harden(&cfg);

    /* TODO: 初始化 q_reactor 事件循环
     * TODO: 通过 q_ipc 监听 Unix Socket（来自 netd 的可信 TLV）
     * TODO: 解析 TLV -> 业务分发（权限 / 审计 / 视频调度 / 数据查询）
     * TODO: 审计日志写入（SM3 链式哈希，留存 >= 180 天）
     */
    while (!daemon_should_stop()) {
        (void)pause(); /* 骨架：真实实现替换为 reactor_run() */
    }

    printf("[bizd] shutting down\n");
    return 0;
}
