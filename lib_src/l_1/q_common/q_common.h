/**
 * @file q_common.h
 * @brief l_1 基础层 守护进程公共件库（libqcommon）对外头文件
 * @layer  lib_src/l_1/q_common  ->  产出 libqcommon
 * @note   职责：信号安装 / 停止标志 / 降权沙箱（chroot + setuid）/
 *         独立晚期 seccomp 沙箱（daemon_seccomp_apply，见下方）。
 *         被 soft_src 下各后台服务（netd / bizd / knockd）共享。
 *         当前仅依赖 libc；seccomp 由本库链接 $(LIB_SECCOMP)（libseccomp），
 *         最终可执行程序务必在其 LIBS 中保留 -lseccomp。
 */
#ifndef L_1_Q_COMMON_H
#define L_1_Q_COMMON_H

#include "include_stdio.h"

/* 降权/沙箱配置（由调用方构造，传指针给 daemon_harden） */
struct daemon_harden_cfg
{
    const char *chroot_dir; /* NULL = 不 chroot */
    int drop_uid;           /* -1 = 不降权；否则切换到该 uid（建议专用非特权用户） */
    int seccomp_profile;    /* 0 = 不启用；非 0 = 按服务选择 seccomp profile（待实现） */
};

/* 安装 SIGTERM/SIGINT 处理，置全局停止标志。成功 0，失败 -1。 */
int daemon_install_signal_handlers(void);

/* 查询是否应停止（信号已触发，供主循环轮询）。 */
int daemon_should_stop(void);

/* 按 cfg 降权：chroot（可选）+ setuid（可选）。
 * 任一步骤失败返回 -1（严重错误，调用方应退出）；全部跳过/成功返回 0。
 * 注意：本函数只做早期降权，【不】安装 seccomp（避免拦死后续资源创建）。 */
int daemon_harden(const struct daemon_harden_cfg *cfg);

/* seccomp 沙箱（独立晚期函数）：必须在所有资源（socket / reactor / 线程池 /
 * 密钥 / log）就绪后、进入事件循环前由各守护进程调用。不要在 daemon_harden
 * 内调用——那在资源创建之前执行，会把自己 setup 期的 syscall 拦死。
 *   profile: 1=netd（多线程 TCP + Unix IPC）, 2=bizd（暂未画像，默认不装）,
 *            3=knockd（单线程 UDP + Netlink 开门）。
 * 返回 0 成功 / -1 失败（调用方通常忽略失败以保持可用，纵深防御失败也应尽量可用）。
 * 环境开关（按服务选变量）：<SVC>_SECCOMP=log 仅记录被拦 syscall；=kill 切 KILL 严模式。 */
enum {
    SECCOMP_PROFILE_NETD   = 1,
    SECCOMP_PROFILE_BIZD   = 2,
    SECCOMP_PROFILE_KNOCKD = 3,
};
int daemon_seccomp_apply(int profile);

#endif /* L_1_Q_COMMON_H */
