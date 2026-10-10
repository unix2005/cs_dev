#ifndef GTK_VMS_DAEMON_SIGNAL_H
#define GTK_VMS_DAEMON_SIGNAL_H

/* 统一安装 SIGINT/SIGTERM 处理：置位停止标志。返回 0 成功，-1 失败。 */
int daemon_install_signal_handlers(void);

/* 查询是否应停止（volatile sig_atomic_t 封装，信号处理安全）。 */
int daemon_should_stop(void);

#endif /* GTK_VMS_DAEMON_SIGNAL_H */
