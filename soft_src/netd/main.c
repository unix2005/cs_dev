/*
 * netd —— 协议进程（公网 TCP）
 *
 * 职责（技术方案 V2 §1.2）：SM2 密钥协商、SM4-GCM 解包、字段校验、限流；
 *       将已认证的业务请求经 Unix Socket（q_ipc）转发给 bizd。
 * 安全基线：非 root + chroot + seccomp；无 DB 凭据、无文件写权限。
 *
 * ===================== 并发模型（关键，务必先读） =====================
 * 本进程【不使用】 q_net_srv，原因是语义冲突：
 *   - q_chan 的 handshake / receive 是【阻塞、多轮往返】语义：握手走裸 DER
 *     字节流（2 字节大端长度 + SM2 DER），且必须握手成功后（established==1）
 *     才允许收发加密帧（见 q_chan_send/recv/transmit/receive 首部守卫）；
 *   - q_net_srv 会把连接 fd 置为非阻塞，且 reactor 已把数据读空后才按 chunk
 *     派发给 worker。后果：非阻塞 fd 让 q_net_recv_blocking 遇 EAGAIN 直接
 *     返回 -1（握手恒失败）；且握手所需字节已被 reactor 消费，等待永不满足。
 * 故自建 accept 循环：
 *     reactor(监听 fd) --accept--> 有界线程池派发【整条连接】
 *       --> worker 线程内跑阻塞式 q_chan 全流程
 *           （握手 -> 收包 -> 状态/配额校验 -> 转发 bizd -> 加密回写）
 * 未认证连接受 §6.4 配额（≤4 包 / ≤8KB / ≤10s）约束，防慢速连接耗尽 worker。
 *
 * 文件划分：
 *   srv_netd.h     公共头（类型 / 常量 / 接口聚合）
 *   main.c         入口：参数解析 + 资源装配 + 主循环 + 清理
 *   netd_config.c  配置加载（q_xcfg）+ SM2 密钥对加载
 *   netd_util.c    时间 / 大端序列化 / socket 超时
 *   netd_limit.c   限流矩阵（IP 并发 / 新建速率 / 登录失败拉黑）
 *   netd_ipc.c     netd -> bizd 转发（自建 TLV 分帧）
 *   netd_push.c    M5 netd_B -> pushd 持久 UDS + 读线程（推送反向通路）
 *   netd_handler.c 业务指令处理器 + 登录流程
 *   netd_srv.c     accept 回调 + 每连接 worker 会话
 */
#include "srv_netd.h"
#include <pwd.h>
#include <sys/types.h>          /* getpwnam / uid_t */

netd_ctx_t g_netd;

static void usage(const char *argv0)
{
    fprintf(stderr, "用法: %s [-c <配置文件>] [-h]\n", argv0);
    fprintf(stderr, "  配置文件优先级: -c  >  $%s  >  %s\n", NETD_CONF_ENV, NETD_CONF_DEF);
}

int main(int argc, char **argv)
{
    const char *conf_path = NULL;
    int  i, rc = 1;
    struct daemon_harden_cfg hc;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            conf_path = argv[++i];
        }
        else
        {
            usage(argv[0]);
            return 1;
        }
    }
    if (!conf_path)
    {
        conf_path = getenv(NETD_CONF_ENV);
        if (!conf_path || !*conf_path)
            conf_path = NETD_CONF_DEF;
    }

    /* 信号处理器最先安装：使 SIGTERM/SIGINT 走"置停止标志"而非默认终止，
       主循环据此优雅退出（与 knockd 一致） */
    if (daemon_install_signal_handlers() != 0)
    {
        fprintf(stderr, "[netd] 信号处理器安装失败\n");
        return 1;
    }

    memset(&g_netd, 0, sizeof(g_netd));
    g_netd.listen_fd  = (q_fd_t)-1;
    g_netd.listen_fd6 = (q_fd_t)-1;

    cfg_defaults();
    (void)load_config(conf_path);

    /* 日志：文件打开失败时 q_log_open 自动回退控制台，句柄恒非空 */
    g_netd.log = q_log_open(g_cfg.log_file[0] ? g_cfg.log_file : NULL,
                            (q_log_level_t)g_cfg.log_level, g_cfg.log_max_size);
    if (!g_netd.log)
    {
        fprintf(stderr, "[netd] 日志初始化失败\n");
        return 1;
    }
    q_log_set_console(g_netd.log, g_cfg.log_console);

    QLOG_INFO(g_netd.log, "netd 启动 (pid=%d, port=%d, workers=%d, queue=%d)",
             (int)getpid(), g_cfg.listen_port, g_cfg.nworkers, g_cfg.max_queue);
    QLOG_INFO(g_netd.log, "配置: ipc=%s push_ipc=%s priv=%s pub=%s",
             g_cfg.ipc_path, g_cfg.push_ipc_path[0] ? g_cfg.push_ipc_path : "(关)",
             g_cfg.priv_file, g_cfg.pub_file);

    /* SM2 服务端长期密钥对（握手身份认证用）；生产环境须离线生成后部署 */
    if (load_sm2_keypair(&g_netd) != 0)
    {
        QLOG_FATAL(g_netd.log, "SM2 密钥对加载失败");
        QLOG_FATAL(g_netd.log, "  私钥: %s", g_cfg.priv_file);
        QLOG_FATAL(g_netd.log, "  公钥: %s（客户端需内置以校验服务端身份）", g_cfg.pub_file);
        goto out_log;
    }

    /* 事件循环：Linux=epoll / macOS=kqueue / Windows=WSAPoll（q_reactor 内部适配） */
    g_netd.reactor = q_reactor_create();
    if (!g_netd.reactor)
    {
        QLOG_FATAL(g_netd.log, "reactor 创建失败");
        goto out_key;
    }

    /* 有界线程池：派发整条连接。max_queue 有界，满则拒绝新连接（防 DoS） */
    g_netd.pool = q_tpool_create(g_cfg.nworkers, g_cfg.max_queue);
    if (!g_netd.pool)
    {
        QLOG_FATAL(g_netd.log, "线程池创建失败");
        goto out_reactor;
    }
    g_netd.key_id = g_cfg.key_id;

    if (pthread_mutex_init(&g_netd.lock, NULL) != 0)
    {
        QLOG_FATAL(g_netd.log, "互斥锁初始化失败");
        goto out_pool;
    }

    /* 公网 TCP 监听（IPv4） */
    if (q_net_bind_listen(g_cfg.listen_port, g_cfg.backlog, &g_netd.listen_fd) != 0)
    {
        QLOG_FATAL(g_netd.log, "监听端口 %d 失败", g_cfg.listen_port);
        goto out_lock;
    }
    (void)q_net_set_nonblock(g_netd.listen_fd);   /* accept 循环取空后返回 EAGAIN */

    /* 降级：IPv6 监听失败仅告警，不影响 IPv4 服务 */
    if (g_cfg.enable_ipv6 &&
        q_net_bind_listen6(g_cfg.listen_port, g_cfg.backlog, &g_netd.listen_fd6) == 0)
    {
        (void)q_net_set_nonblock(g_netd.listen_fd6);
    }
    else
    {
        g_netd.listen_fd6 = (q_fd_t)-1;
        if (g_cfg.enable_ipv6)
            QLOG_WARN(g_netd.log, "IPv6 监听未启用（创建失败）");
    }

    if (q_reactor_add(g_netd.reactor, g_netd.listen_fd, EPOLLIN, on_accept, &g_netd) != 0)
    {
        QLOG_FATAL(g_netd.log, "监听 fd 注册 reactor 失败");
        goto out_fd;
    }
    if (g_netd.listen_fd6 != (q_fd_t)-1 &&
        q_reactor_add(g_netd.reactor, g_netd.listen_fd6, EPOLLIN, on_accept, &g_netd) != 0)
    {
        QLOG_WARN(g_netd.log, "IPv6 监听 fd 注册 reactor 失败");
    }

    /* 降权沙箱：所有资源就绪后、进入事件循环前。
       chroot / drop 由配置驱动（默认空 = 不启用，便于开发/测试）。
       注意：启用 chroot 后，与 bizd 的 Unix Socket（/var/run/gtkvms/bizd.sock）
       在 jail 内不可见，生产部署须将该 socket 目录 bind-mount 进 chroot_dir。 */
    memset(&hc, 0, sizeof(hc));
    hc.chroot_dir = g_cfg.chroot_dir[0] ? g_cfg.chroot_dir : NULL;
    hc.drop_uid   = -1;
    if (g_cfg.drop_user[0])
    {
        struct passwd *pw = getpwnam(g_cfg.drop_user);
        if (pw)
            hc.drop_uid = (int)pw->pw_uid;
        else
            QLOG_WARN(g_netd.log, "drop_user '%s' 不存在，维持当前身份运行", g_cfg.drop_user);
    }
    hc.seccomp_profile = SECCOMP_PROFILE_NETD;  /* netd：仅记录，daemon_harden 不据此装沙箱 */
    if (daemon_harden(&hc) != 0)
    {
        QLOG_FATAL(g_netd.log, "降权/沙箱生效失败");
        goto out_fd;
    }

    QLOG_INFO(g_netd.log, "进入事件循环");
    /* seccomp 沙箱：资源就绪后、事件循环前安装（netd 多线程须放行 clone；
       实现位于共享库 libqcommon daemon_seccomp_apply，profile=1=netd）。 */
    (void)daemon_seccomp_apply(SECCOMP_PROFILE_NETD);

    while (!daemon_should_stop())
    {
        (void)q_reactor_run(g_netd.reactor, g_cfg.poll_ms);
    }

    QLOG_INFO(g_netd.log, "netd 退出");
    rc = 0;

out_fd:
    if (g_netd.listen_fd != (q_fd_t)-1)
        q_net_close(g_netd.listen_fd);
    if (g_netd.listen_fd6 != (q_fd_t)-1)
        q_net_close(g_netd.listen_fd6);
out_lock:
    pthread_mutex_destroy(&g_netd.lock);
out_pool:
    if (g_netd.pool)
        q_tpool_destroy(g_netd.pool);     /* 等待在途连接处理完 */
out_reactor:
    if (g_netd.reactor)
        q_reactor_destroy(g_netd.reactor);
out_key:
    if (g_netd.sm2_priv)
    {
        q_mem_zero(g_netd.sm2_priv, g_netd.sm2_priv_len);
        free(g_netd.sm2_priv);
    }
    if (g_netd.sm2_pub)
        free(g_netd.sm2_pub);
out_log:
    if (g_netd.log)
        q_log_close(g_netd.log);
    return rc;
}
