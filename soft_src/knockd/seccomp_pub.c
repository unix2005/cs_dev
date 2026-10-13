#include "srv_knockd.h"

/* P0 seccomp 沙箱：libseccomp 收敛 syscall 攻击面。
   设计：
   - 默认动作 SCMP_ACT_ERRNO(EPERM)：未知 syscall 被拒但进程不崩（部署安全，
     避免漏枚举到的良性 syscall 误杀守护进程；验证充分后可切 KILL）。
   - 显式 KILL：execve/execveat/fork/vfork/ptrace/bpf 等"代码执行/进程克隆"原语
     （首个接触攻击者的 UDP 解析器一旦被 RCE，无法 spawn shell / 新进程 / 改过滤器）。
   - 允许列表：事件循环 + libnftables(Netlink) + Tongsuo(国密) + glibc 实际用到的 syscall。
   环境开关（运维排障/渐进收紧）：
     KNOCKD_SECCOMP=log  : 默认动作 SCMP_ACT_LOG（仅记录被拦 syscall，不阻断）
     KNOCKD_SECCOMP=kill : 默认动作 SCMP_ACT_KILL_PROCESS（fail-closed，生产严模式）
   调用时机：必须在所有资源（socket/reactor/log/nft 上下文）就绪后、进入事件循环前
   安装——否则 setup 期的 socket()/epoll_create() 会被自身沙箱拦死。 */



int knockd_install_seccomp(q_log_t *log)
{
    uint32_t def = SCMP_ACT_ERRNO(EPERM);
    const char *m = getenv("KNOCKD_SECCOMP");
    if (m)
    {
        if (strcmp(m, "log") == 0)
            def = SCMP_ACT_LOG;
        else if (strcmp(m, "kill") == 0)
            def = SCMP_ACT_KILL_PROCESS;
    }

    scmp_filter_ctx ctx = seccomp_init(def);
    if (ctx == NULL)
    {
        QLOG_ERROR(log, "seccomp_init 失败（libseccomp/内核不支持）");
        return -1;
    }

    /* 允许列表（ALLOW）：事件循环 + 国密/Tongsuo + libnftables(Netlink) + glibc */
    static const int allow[] = {
        /* 基础 IO / 日志 */
        SCMP_SYS(read), SCMP_SYS(readv), SCMP_SYS(write), SCMP_SYS(writev),
        SCMP_SYS(close), SCMP_SYS(fstat), SCMP_SYS(lseek), SCMP_SYS(ftruncate),
        SCMP_SYS(fsync), SCMP_SYS(fcntl), SCMP_SYS(open), SCMP_SYS(openat),
        SCMP_SYS(stat), SCMP_SYS(newfstatat),
        /* 网络收发（地址族限定另见下方 socket 规则；bind/connect 等不限定） */
        SCMP_SYS(bind), SCMP_SYS(setsockopt), SCMP_SYS(getsockopt),
        SCMP_SYS(connect), SCMP_SYS(getsockname), SCMP_SYS(getpeername),
        SCMP_SYS(shutdown), SCMP_SYS(recvfrom), SCMP_SYS(recvmsg),
        SCMP_SYS(sendto), SCMP_SYS(sendmsg),
        /* 事件循环（q_reactor 实际用 poll；epoll_* 一并允许以兼容其他实现） */
        SCMP_SYS(epoll_create), SCMP_SYS(epoll_create1), SCMP_SYS(epoll_ctl), SCMP_SYS(epoll_wait),
        SCMP_SYS(poll), SCMP_SYS(ppoll),
        /* 内存（国密/Tongsuo 与 libc 运行期分配） */
        SCMP_SYS(mmap), SCMP_SYS(munmap), SCMP_SYS(mremap), SCMP_SYS(brk),
        SCMP_SYS(madvise), SCMP_SYS(mprotect),
        /* 时间与信号（含信号投递所需的 rt_sigreturn） */
        SCMP_SYS(clock_gettime), SCMP_SYS(clock_getres), SCMP_SYS(gettimeofday),
        SCMP_SYS(nanosleep), SCMP_SYS(rt_sigaction), SCMP_SYS(rt_sigprocmask),
        SCMP_SYS(rt_sigreturn), SCMP_SYS(sigreturn), SCMP_SYS(sigaltstack),
        /* 进程/线程基础设施（单线程：clone 不显式允许，默认 EPERM 即禁止起线程） */
        SCMP_SYS(exit), SCMP_SYS(exit_group), SCMP_SYS(futex), SCMP_SYS(sched_yield),
        SCMP_SYS(restart_syscall), SCMP_SYS(getpid), SCMP_SYS(gettid),
        SCMP_SYS(getuid), SCMP_SYS(geteuid), SCMP_SYS(getgid), SCMP_SYS(getegid), SCMP_SYS(getgroups),
        SCMP_SYS(getrandom), SCMP_SYS(tgkill), SCMP_SYS(tkill),
        SCMP_SYS(prlimit64), SCMP_SYS(arch_prctl), SCMP_SYS(set_robust_list),
        SCMP_SYS(get_robust_list), SCMP_SYS(rseq), SCMP_SYS(membarrier),
    };
    for (size_t i = 0; i < sizeof(allow) / sizeof(allow[0]); i++)
    {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, allow[i], 0) < 0)
            goto fail;
    }

    /* socket 限定于可信地址族（AF_INET=2 / AF_INET6=10 / AF_NETLINK=16），
       阻断 AF_PACKET/原始套接字等（开门的 Netlink 走 AF_NETLINK，setup 期
       v4/v6 套接字已在安装前建好，此处仅覆盖运行期新开的 Netlink）。 */
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
                         SCMP_A0(SCMP_CMP_EQ, AF_INET)) < 0)  goto fail;
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
                         SCMP_A0(SCMP_CMP_EQ, AF_INET6)) < 0) goto fail;
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
                         SCMP_A0(SCMP_CMP_EQ, AF_NETLINK)) < 0) goto fail;

    /* 显式 KILL：代码执行 / 进程克隆 / 调试 / 内核篡改原语（fail-closed） */
    static const int kill_sys[] = {
        SCMP_SYS(execve), SCMP_SYS(execveat), SCMP_SYS(fork), SCMP_SYS(vfork),
        SCMP_SYS(ptrace), SCMP_SYS(clone), SCMP_SYS(clone3),
        SCMP_SYS(bpf), SCMP_SYS(process_vm_readv), SCMP_SYS(process_vm_writev),
        SCMP_SYS(mount), SCMP_SYS(umount), SCMP_SYS(umount2),
        SCMP_SYS(kexec_load), SCMP_SYS(kexec_file_load),
        SCMP_SYS(init_module), SCMP_SYS(finit_module), SCMP_SYS(delete_module),
        SCMP_SYS(iopl), SCMP_SYS(ioperm), SCMP_SYS(swapon), SCMP_SYS(swapoff),
        SCMP_SYS(settimeofday), SCMP_SYS(clock_settime), SCMP_SYS(chroot),
        SCMP_SYS(personality), SCMP_SYS(reboot), SCMP_SYS(acct),
        SCMP_SYS(perf_event_open), SCMP_SYS(fanotify_init), SCMP_SYS(fanotify_mark),
    };
    for (size_t i = 0; i < sizeof(kill_sys) / sizeof(kill_sys[0]); i++)
    {
        if (seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, kill_sys[i], 0) < 0)
            goto fail;
    }

    if (seccomp_load(ctx) < 0)
    {
        QLOG_ERROR(log, "seccomp_load 失败（需 CAP_SYS_ADMIN 或内核允许 unprivileged seccomp）");
        seccomp_release(ctx);
        return -1;
    }
    seccomp_release(ctx);
    QLOG_INFO(log, "seccomp 沙箱已启用 (default=%s)",
              def == SCMP_ACT_KILL_PROCESS ? "KILL" : (def == SCMP_ACT_LOG ? "LOG" : "ERRNO(EPERM)"));
    return 0;

fail:
    QLOG_ERROR(log, "seccomp 规则构造失败");
    seccomp_release(ctx);
    return -1;
}
