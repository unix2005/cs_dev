#ifndef GTK_VMS_DAEMON_HARDEN_H
#define GTK_VMS_DAEMON_HARDEN_H

/* 进程降权与沙箱配置（按守护进程角色填不同值） */
struct daemon_harden_cfg {
    const char *chroot_dir;      /* 非空则 chroot 到该空目录 */
    int         drop_uid;        /* >=0 则 setresuid 到该 uid（专用非特权用户） */
    int         seccomp_profile; /* 0=不加载; 1=netd; 2=bizd; 3=knockd（占位，后续接 libseccomp） */
};

/* 按配置降权 + 沙箱。Linux 下生效，其余平台为 no-op。返回 0 成功。 */
int daemon_harden(const struct daemon_harden_cfg *cfg);

#endif /* GTK_VMS_DAEMON_HARDEN_H */
