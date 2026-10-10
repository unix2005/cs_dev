/*
 * netd —— 协议进程（公网 TCP）
 *
 * 职责：SM2 密钥协商、SM4-GCM 解包、字段校验、限流；将已认证的业务请求
 *      经 Unix Socket（q_ipc）转发给 bizd。
 * 安全基线：非 root + chroot + seccomp；无 DB 凭据、无文件写权限。
 *
 * 骨架：主循环 + 降权/沙箱占位（逻辑后续填充）。重复样板已抽至
 *       ../../common（daemon_signal / daemon_harden）。
 */
#include <stdio.h>
#include <unistd.h>
#include "daemon_signal.h"
#include "daemon_harden.h"

int main(void)
{
    if (daemon_install_signal_handlers() != 0) {
        fprintf(stderr, "[netd] 信号处理器安装失败\n");
        return 1;
    }

    printf("[netd] skeleton start (pid=%d)\n", (int)getpid());

    static const struct daemon_harden_cfg cfg = {
        .chroot_dir      = "/var/empty/netd",
        .drop_uid        = -1, /* TODO: 改为专用非特权用户 netd 的 uid */
        .seccomp_profile = 1,  /* netd：收口不可信网络输入解析 */
    };
    daemon_harden(&cfg);

    /* TODO: 初始化 q_reactor 事件循环
     * TODO: 创建公网 TCP 监听套接字（q_net）
     * TODO: 接入 q_chan：新连接 -> HELLO / KEY_EXCHANGE / LOGIN 握手
     * TODO: 认证后注册指令分发；未认证阶段走 q_sec 限流矩阵
     * TODO: 业务请求经 q_ipc(Unix Socket) 转发 bizd
     */
    while (!daemon_should_stop()) {
        (void)pause(); /* 骨架：真实实现替换为 reactor_run() */
    }

    printf("[netd] shutting down\n");
    return 0;
}
