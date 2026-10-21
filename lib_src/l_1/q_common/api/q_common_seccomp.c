/* 守护进程 seccomp 沙箱（独立晚期函数，与 daemon_harden 分离）：
   必须在所有资源（socket / reactor / 线程池 / 密钥 / log）就绪后、进入事件循环前
   由各守护进程调用。不要塞进 daemon_harden——那在资源创建之前执行，会把自己
   setup 期的 socket()/bind()/accept()/pthread_create() 拦死。

   设计：
   - 默认动作 SCMP_ACT_ERRNO(EPERM)：未知 syscall 被拒但进程不崩（部署安全，
     避免漏枚举到的良性 syscall 误杀守护进程；验证充分后可切 KILL）。
   - 显式 KILL：execve/execveat/fork/vfork/ptrace/bpf 等"代码执行/进程克隆"原语
     （首个接触攻击者的网络解析器一旦被 RCE，无法 spawn shell / 新进程 / 改过滤器）。
   - 按 profile 收敛允许列表：
       profile 1 (netd)   : 多线程 TCP + Unix Socket IPC（放行 clone；AF_INET/INET6/UNIX）
       profile 2 (bizd)   : 暂未画像；默认不安装，除非显式置 BIZD_SECCOMP=log|kill
       profile 3 (knockd) : 单线程 UDP + libnftables(Netlink) 开门（AF_INET/INET6/NETLINK）
   环境开关（运维排障/渐进收紧，按服务选变量）：
     KNOCKD_SECCOMP / NETD_SECCOMP / BIZD_SECCOMP = log  : 默认动作 SCMP_ACT_LOG
                                                  = kill : 默认动作 SCMP_ACT_KILL_PROCESS
   日志：本库仅依赖 libc，直接写 stderr（守护进程 stderr 通常已重定向至日志）。 */
#include "headers.h"
#include "q_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
/* 仅 Linux 才有 <seccomp.h>（libseccomp）；macOS/Windows 无此头，
   必须一并收进 Q_SYS_LINUX 分支，否则 macOS 编译在此处直接失败。 */
#ifdef Q_SYS_LINUX
#include <sys/socket.h>
#include <seccomp.h>
#endif

#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef AF_NETLINK
#define AF_NETLINK 16
#endif

/* ===== 以下实现依赖 Linux seccomp-bpf（libseccomp），非 Linux 平台整体不编译 ===== */
#ifdef Q_SYS_LINUX

/* 允许列表维度开关 */
#define SC_NET      (1u << 0)  /* recvfrom/recvmsg/sendto/sendmsg/recv/send */
#define SC_ACCEPT   (1u << 1)  /* TCP accept/accept4 */
#define SC_CLONE    (1u << 2)  /* 多线程：clone/clone3 放行（pthread 内部） */
#define SC_NETLINK  (1u << 3)  /* libnftables 开门：AF_NETLINK */
#define SC_UNIX     (1u << 4)  /* Unix Socket IPC：AF_UNIX */

/* 基础允许列表（所有服务共用：IO/日志/内存/时间/信号/进程线程基础设施） */
static int add_base_allow(scmp_filter_ctx ctx)
{
    static const int base[] = {
        SCMP_SYS(read), SCMP_SYS(readv), SCMP_SYS(write), SCMP_SYS(writev),
        SCMP_SYS(close), SCMP_SYS(fstat), SCMP_SYS(lseek), SCMP_SYS(ftruncate),
        SCMP_SYS(fsync), SCMP_SYS(fcntl), SCMP_SYS(open), SCMP_SYS(openat),
        SCMP_SYS(stat), SCMP_SYS(newfstatat),
        SCMP_SYS(rename), SCMP_SYS(renameat), SCMP_SYS(renameat2),
        SCMP_SYS(bind), SCMP_SYS(listen), SCMP_SYS(connect),
        SCMP_SYS(getsockname), SCMP_SYS(getpeername),
        SCMP_SYS(setsockopt), SCMP_SYS(getsockopt), SCMP_SYS(shutdown),
        /* 网络收发原语：netd(TCP)/bizd(UDS+下游TCP)/knockd(UDP) 均需要，
           放入 base 无条件放行，避免 profile 标志未覆盖导致 recvfrom/send 等被拦。 */
        SCMP_SYS(recv), SCMP_SYS(recvfrom), SCMP_SYS(recvmsg),
        SCMP_SYS(send), SCMP_SYS(sendto), SCMP_SYS(sendmsg),
        SCMP_SYS(epoll_create), SCMP_SYS(epoll_create1), SCMP_SYS(epoll_ctl), SCMP_SYS(epoll_wait),
        SCMP_SYS(poll), SCMP_SYS(ppoll),
        SCMP_SYS(mmap), SCMP_SYS(munmap), SCMP_SYS(mremap), SCMP_SYS(brk),
        SCMP_SYS(madvise), SCMP_SYS(mprotect),
        SCMP_SYS(clock_gettime), SCMP_SYS(clock_getres), SCMP_SYS(gettimeofday),
        SCMP_SYS(nanosleep), SCMP_SYS(rt_sigaction), SCMP_SYS(rt_sigprocmask),
        SCMP_SYS(rt_sigreturn), SCMP_SYS(sigreturn), SCMP_SYS(sigaltstack),
        SCMP_SYS(exit), SCMP_SYS(exit_group), SCMP_SYS(futex), SCMP_SYS(sched_yield),
        SCMP_SYS(restart_syscall), SCMP_SYS(getpid), SCMP_SYS(gettid),
        SCMP_SYS(getuid), SCMP_SYS(geteuid), SCMP_SYS(getgid), SCMP_SYS(getegid), SCMP_SYS(getgroups),
        SCMP_SYS(getrandom), SCMP_SYS(tgkill), SCMP_SYS(tkill),
        SCMP_SYS(prlimit64), SCMP_SYS(arch_prctl), SCMP_SYS(set_robust_list),
        SCMP_SYS(get_robust_list), SCMP_SYS(rseq), SCMP_SYS(membarrier),
    };
    for (size_t i = 0; i < sizeof(base) / sizeof(base[0]); i++)
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, base[i], 0) < 0)
            return -1;
    return 0;
}

/* 追加按 profile 差异化的允许项（clone / 收发包 / accept） */
static int add_profile_allow(scmp_filter_ctx ctx, unsigned flags)
{
    if (flags & SC_CLONE) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clone), 0) < 0)  return -1;
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clone3), 0) < 0) return -1;
    }
    if (flags & SC_NET) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvfrom), 0) < 0) return -1;
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recvmsg), 0)  < 0) return -1;
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendto), 0)  < 0) return -1;
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sendmsg), 0) < 0) return -1;
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(recv), 0)     < 0) return -1;
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(send), 0)     < 0) return -1;
    }
    if (flags & SC_ACCEPT) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(accept), 0)  < 0) return -1;
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(accept4), 0) < 0) return -1;
    }
    return 0;
}

/* socket 地址族限定：只允许可信族，阻断 AF_PACKET/原始套接字等 */
static int add_socket_families(scmp_filter_ctx ctx, unsigned flags)
{
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
                         SCMP_A0(SCMP_CMP_EQ, AF_INET)) < 0)  return -1;
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
                         SCMP_A0(SCMP_CMP_EQ, AF_INET6)) < 0) return -1;
    if (flags & SC_NETLINK)
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
                             SCMP_A0(SCMP_CMP_EQ, AF_NETLINK)) < 0) return -1;
    if (flags & SC_UNIX)
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(socket), 1,
                             SCMP_A0(SCMP_CMP_EQ, AF_UNIX)) < 0) return -1;
    return 0;
}

/* 显式 KILL：代码执行 / 进程克隆 / 调试 / 内核篡改原语（fail-closed）。
   注：clone 由 profile 按需 ALLOW，此处跳过（多线程服务不能 KILL clone）。 */
static int add_kill_list(scmp_filter_ctx ctx, unsigned flags)
{
    static const int kill_sys[] = {
        SCMP_SYS(execve), SCMP_SYS(execveat), SCMP_SYS(fork), SCMP_SYS(vfork),
        SCMP_SYS(ptrace), SCMP_SYS(bpf), SCMP_SYS(process_vm_readv), SCMP_SYS(process_vm_writev),
        SCMP_SYS(mount), SCMP_SYS(umount), SCMP_SYS(umount2),
        SCMP_SYS(kexec_load), SCMP_SYS(kexec_file_load),
        SCMP_SYS(init_module), SCMP_SYS(finit_module), SCMP_SYS(delete_module),
        SCMP_SYS(iopl), SCMP_SYS(ioperm), SCMP_SYS(swapon), SCMP_SYS(swapoff),
        SCMP_SYS(settimeofday), SCMP_SYS(clock_settime), SCMP_SYS(chroot),
        SCMP_SYS(personality), SCMP_SYS(reboot), SCMP_SYS(acct),
        SCMP_SYS(perf_event_open), SCMP_SYS(fanotify_init), SCMP_SYS(fanotify_mark),
    };
    for (size_t i = 0; i < sizeof(kill_sys) / sizeof(kill_sys[0]); i++) {
        int nr = kill_sys[i];
        if ((nr == SCMP_SYS(clone) || nr == SCMP_SYS(clone3)) && (flags & SC_CLONE))
            continue; /* 多线程服务：clone 已 ALLOW */
        if (seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, nr, 0) < 0)
            return -1;
    }
    return 0;
}

int daemon_seccomp_apply(int profile)
{
    const char *envname = NULL;
    unsigned flags = 0;
    switch (profile) {
    case 1:  /* netd: 多线程 TCP + Unix Socket IPC */
        envname = "NETD_SECCOMP";
        flags = SC_NET | SC_ACCEPT | SC_CLONE | SC_UNIX;
        break;
    case 2:  /* bizd: UDS 服务端 + 下游 TCP 转发，需 accept + 收发包 */
        envname = "BIZD_SECCOMP";
        flags = SC_NET | SC_ACCEPT | SC_CLONE | SC_UNIX;
        break;
    case 3:  /* knockd: 单线程 UDP + Netlink 开门 */
        envname = "KNOCKD_SECCOMP";
        flags = SC_NET | SC_NETLINK;
        break;
    default:
        fprintf(stderr, "[seccomp] 未知 profile=%d，跳过安装\n", profile);
        return 0;
    }

    /* bizd 未画像：默认不安装，避免误杀；显式置 env 才强制装 */
    if (profile == 2 && getenv(envname) == NULL) {
        fprintf(stderr, "[seccomp] bizd(profile=2) 未画像，默认不安装"
                        "（置 BIZD_SECCOMP=log|kill 可强制）\n");
        return 0;
    }

    uint32_t def = SCMP_ACT_ERRNO(EPERM);
    const char *m = getenv(envname);
    if (m) {
        if (strcmp(m, "log") == 0)
            def = SCMP_ACT_LOG;
        else if (strcmp(m, "kill") == 0)
            def = SCMP_ACT_KILL_PROCESS;
    }

    scmp_filter_ctx ctx = seccomp_init(def);
    if (ctx == NULL) {
        fprintf(stderr, "[seccomp] seccomp_init 失败（libseccomp/内核不支持）\n");
        return -1;
    }

    if (add_base_allow(ctx) < 0)            goto fail;
    if (add_profile_allow(ctx, flags) < 0)  goto fail;
    if (add_socket_families(ctx, flags) < 0) goto fail;
    /* 显式 KILL 列表仅在 default 非 KILL 时添加：
       - default=ERRNO/LOG：把 execve/fork/clone 等危险原语单独 KILL，
         其余 syscall 仅被 ERRNO/LOG 处理（失败-封闭前的渐进收敛）；
       - default=KILL_PROCESS：未放行 syscall 已被默认动作 KILL，再添加
         KILL 规则会与默认动作冲突（libseccomp 报 EINVAL 导致构造失败），故跳过。 */
    if (def != SCMP_ACT_KILL_PROCESS) {
        if (add_kill_list(ctx, flags) < 0)  goto fail;
    }

    if (seccomp_load(ctx) < 0) {
        fprintf(stderr, "[seccomp] seccomp_load 失败"
                        "（需 CAP_SYS_ADMIN 或内核允许 unprivileged seccomp）\n");
        seccomp_release(ctx);
        return -1;
    }
    seccomp_release(ctx);
    fprintf(stderr, "[seccomp] 沙箱已启用 profile=%d default=%s\n", profile,
            def == SCMP_ACT_KILL_PROCESS ? "KILL" :
            (def == SCMP_ACT_LOG ? "LOG" : "ERRNO(EPERM)"));
    return 0;

fail:
    fprintf(stderr, "[seccomp] 规则构造失败\n");
    seccomp_release(ctx);
    return -1;
}

#else  /* 非 Linux（Windows / macOS）：无 seccomp-bpf，提供空实现桩 */

int daemon_seccomp_apply(int profile)
{
    /* 平台不支持 seccomp-bpf；纵深防御项缺失，调用方通常忽略失败以保持可用。 */
    (void)profile;
    fprintf(stderr, "[seccomp] 当前平台不支持 seccomp-bpf（仅 Linux 可用），已跳过\n");
    return -1;
}

#endif /* Q_SYS_LINUX */
