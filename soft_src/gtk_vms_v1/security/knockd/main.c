/*
 * knockd —— SPA 单包授权进程（UDP，仅 CAP_NET_ADMIN）
 *
 * 职责：接收运维终端发来的 UDP 单包授权（SM4-GCM 加密 + SM2 签名，由 q_spa 校验）；
 *      校验通过将源 IP 加入本机 nftables allow set（TTL 300s 自动过期），
 *      使后续 TCP 私有协议才能抵达 netd。
 *
 * 安全基线：代码量 < 500 行，可逐行审查；是首个接触攻击者的代码，
 *         须配 seccomp 严格收敛 syscall，并纳入 AFL++/libFuzzer 7x24 零崩溃验证。
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
        fprintf(stderr, "[knockd] 信号处理器安装失败\n");
        return 1;
    }

    printf("[knockd] skeleton start (pid=%d)\n", (int)getpid());

    static const struct daemon_harden_cfg cfg = {
        .chroot_dir      = NULL, /* knockd 需 nftables 操作，不做 chroot */
        .drop_uid        = -1,   /* TODO: 仍降为非 root，仅保留 CAP_NET_ADMIN */
        .seccomp_profile = 3,    /* knockd：仅 socket/recvfrom/setsockopt + nftables 所需 */
    };
    daemon_harden(&cfg);

    /* TODO: 创建 UDP 监听套接字（固定单端口）
     * TODO: recvfrom 收取 SPA 包 -> q_spa_verify 校验（恒定时间，防时序差异）
     * TODO: 校验通过 -> nft add element inet spa allow4 { <src_ip> timeout 300s }
     * TODO: 校验失败 -> 静默丢弃（不回包，抗扫描）
     */
    while (!daemon_should_stop()) {
        (void)pause(); /* 骨架：真实实现替换为 reactor_run() 或阻塞 recvfrom 循环 */
    }

    printf("[knockd] shutting down\n");
    return 0;
}
