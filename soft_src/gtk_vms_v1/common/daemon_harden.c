#include "daemon_harden.h"

int daemon_harden(const struct daemon_harden_cfg *cfg)
{
    (void)cfg;
#ifdef __linux__
    /* TODO: if (cfg->chroot_dir) chroot(cfg->chroot_dir); */
    /* TODO: if (cfg->drop_uid >= 0) setresuid(cfg->drop_uid, cfg->drop_uid, cfg->drop_uid); */
    /* TODO: if (cfg->seccomp_profile) load_seccomp_filter(cfg->seccomp_profile); */
#endif
    return 0;
}
