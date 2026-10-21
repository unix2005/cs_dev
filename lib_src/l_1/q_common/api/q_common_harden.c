/* 守护进程降权/沙箱骨架：chroot（可选）+ setuid（可选）；seccomp 待实现 */
#include "headers.h"
#include "q_common.h"

int daemon_harden(const struct daemon_harden_cfg *cfg)
{
    if (cfg == NULL)
    {
        return -1;
    }

#ifdef Q_SYS_WINDOWS
    /* Windows 无 chroot / setuid（服务降权走服务账户或完整性级别），
       一旦调用方要求这两项即明确失败，避免“假装加固成功”。 */
    if (cfg->chroot_dir != NULL)
    {
        fprintf(stderr, "[harden] 当前平台不支持 chroot（Windows）\n");
        return -1;
    }
    if (cfg->drop_uid >= 0)
    {
        fprintf(stderr, "[harden] 当前平台不支持 setuid 降权（Windows）\n");
        return -1;
    }
    (void)cfg->seccomp_profile;
    return 0;
#else
    if (cfg->chroot_dir != NULL)
    {
        /* TODO: 正式实现应先绑定挂载最小根文件系统再 chroot，避免信息泄露 */
        if (chroot(cfg->chroot_dir) != 0)
        {
            return -1;
        }
        if (chdir("/") != 0)
        {
            return -1;
        }
    }

    if (cfg->drop_uid >= 0)
    {
        /* TODO: 建议先 setgroups/setgid 再 setuid，防止权限残留 */
        if (setuid((uid_t)cfg->drop_uid) != 0)
        {
            return -1;
        }
    }

    /* seccomp 已由独立的【晚期】函数 daemon_seccomp_apply(cfg->seccomp_profile)
     * 实现（见 q_common_seccomp.c），由各守护进程在资源就绪后、事件循环前调用。
     * 注意：绝不能在此处（资源创建之前）安装 seccomp，否则会拦死自身的
     * socket()/bind()/pthread_create() 等 setup 动作。故此处保持 no-op。 */
    (void)cfg->seccomp_profile;

    return 0;
#endif /* !Q_SYS_WINDOWS */
}
