/* 守护进程降权/沙箱骨架：chroot（可选）+ setuid（可选）；seccomp 待实现 */
#include "headers.h"
#include "q_common.h"

int daemon_harden(const struct daemon_harden_cfg *cfg)
{
    if (cfg == NULL)
    {
        return -1;
    }

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

    /* TODO: 按 cfg->seccomp_profile 应用 libseccomp 收敛 syscall；
     *       实现后本库 Makefile 追加 LIBS += $(LIB_SECCOMP)（-lseccomp）。 */
    (void)cfg->seccomp_profile;

    return 0;
}
