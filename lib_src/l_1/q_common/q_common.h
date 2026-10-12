/**
 * @file q_common.h
 * @brief l_1 基础层 守护进程公共件库（libqcommon）对外头文件
 * @layer  lib_src/l_1/q_common  ->  产出 libqcommon
 * @note   职责：信号安装 / 停止标志 / 降权沙箱（chroot + setuid；seccomp 待实现）。
 *         被 soft_src 下各后台服务（netd / bizd / knockd）共享。
 *         当前仅依赖 libc；seccomp 实现后由本库链接 $(LIB_SECCOMP)（libseccomp）。
 */
#ifndef L_1_Q_COMMON_H
#define L_1_Q_COMMON_H

#include "include_stdio.h"

/* 降权/沙箱配置（由调用方构造，传指针给 daemon_harden） */
struct daemon_harden_cfg {
    const char *chroot_dir;     /* NULL = 不 chroot */
    int         drop_uid;       /* -1 = 不降权；否则切换到该 uid（建议专用非特权用户） */
    int         seccomp_profile;/* 0 = 不启用；非 0 = 按服务选择 seccomp profile（待实现） */
};

/* 安装 SIGTERM/SIGINT 处理，置全局停止标志。成功 0，失败 -1。 */
int daemon_install_signal_handlers(void);

/* 查询是否应停止（信号已触发，供主循环轮询）。 */
int daemon_should_stop(void);

/* 按 cfg 降权：chroot（可选）+ setuid（可选）。seccomp 待实现（见 TODO）。
 * 任一步骤失败返回 -1（严重错误，调用方应退出）；全部跳过/成功返回 0。 */
int daemon_harden(const struct daemon_harden_cfg *cfg);

#endif /* L_1_Q_COMMON_H */
