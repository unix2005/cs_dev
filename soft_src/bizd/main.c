/*
 * bizd —— 业务进程（仅 Unix Socket，不直接暴露公网）
 *
 * 职责：接收 netd 经 q_ipc 转发的【可信 TLV】业务请求，转换为 JSON 信封后
 *       经 TCP 下发给具体干活的微服务（在另一项目实现），再把微服务的 JSON
 *       响应封装回外层 TLV 回传 netd。
 *       DB 凭据、业务逻辑、数据落地均在下游微服务侧；本进程不持有 DB 凭据。
 * 安全基线：虽入站为可信 TLV，仍按 q_codec 字段校验表做结构校验，绝不信任原始字节。
 *
 * 文件划分（与 netd 对称）：
 *   srv_bizd.h      公共头（类型 / 常量 / 接口聚合）
 *   main.c          入口：参数解析 + 资源装配 + 主循环 + 清理
 *   bizd_config.c   配置加载（q_xcfg）
 *   bizd_util.c     大端序列化 / 分帧读写
 *   bizd_route.c    下游微服务路由 + TCP 转发（JSON）
 *   bizd_handler.c  业务指令分发与请求 / 响应组装
 *   bizd_srv.c      UDS 监听 / accept 回调 / 每连接 worker
 */
#include "srv_bizd.h"

bizd_ctx_t g_bizd;

static void usage(const char *argv0)
{
    fprintf(stderr, "用法: %s [-c <配置文件>] [-h]\n", argv0);
    fprintf(stderr, "  配置文件优先级: -c  >  $%s  >  %s\n", BIZD_CONF_ENV, BIZD_CONF_DEF);
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
        conf_path = getenv(BIZD_CONF_ENV);
        if (!conf_path || !*conf_path)
            conf_path = BIZD_CONF_DEF;
    }

    memset(&g_bizd, 0, sizeof(g_bizd));
    g_bizd.listen_fd = -1;

    bizd_cfg_defaults();
    (void)bizd_load_config(conf_path);

    /* 日志：文件打开失败时 q_log_open 自动回退控制台，句柄恒非空 */
    g_bizd.log = q_log_open(g_bcfg.log_file[0] ? g_bcfg.log_file : NULL,
                           (q_log_level_t)g_bcfg.log_level, g_bcfg.log_max_size);
    if (!g_bizd.log)
    {
        fprintf(stderr, "[bizd] 日志初始化失败\n");
        return 1;
    }
    q_log_set_console(g_bizd.log, g_bcfg.log_console);

    QLOG_INFO(g_bizd.log, "bizd 启动 (pid=%d, ipc=%s, workers=%d, queue=%d)",
             (int)getpid(), g_bcfg.ipc_path, g_bcfg.nworkers, g_bcfg.max_queue);

    /* 事件循环 */
    g_bizd.reactor = q_reactor_create();
    if (!g_bizd.reactor)
    {
        QLOG_FATAL(g_bizd.log, "reactor 创建失败");
        goto out_log;
    }

    /* 有界线程池：队列满则拒绝，防 DoS */
    g_bizd.pool = q_tpool_create(g_bcfg.nworkers, g_bcfg.max_queue);
    if (!g_bizd.pool)
    {
        QLOG_FATAL(g_bizd.log, "线程池创建失败");
        goto out_reactor;
    }

    if (pthread_mutex_init(&g_bizd.lock, NULL) != 0)
    {
        QLOG_FATAL(g_bizd.log, "互斥锁初始化失败");
        goto out_pool;
    }

    /* UDS 监听（netd 侧以同一路径 q_ipc_connect） */
    if (q_ipc_listen(g_bcfg.ipc_path, g_bcfg.backlog, &g_bizd.listen_fd) != 0)
    {
        QLOG_FATAL(g_bizd.log, "监听 UDS 失败: %s", g_bcfg.ipc_path);
        goto out_lock;
    }

    if (q_reactor_add(g_bizd.reactor, (q_fd_t)g_bizd.listen_fd, EPOLLIN,
                  bizd_on_accept, &g_bizd) != 0)
    {
        QLOG_FATAL(g_bizd.log, "UDS 监听 fd 注册 reactor 失败");
        goto out_fd;
    }

    /* 降权沙箱：所有资源就绪后、进入事件循环前 */
    memset(&hc, 0, sizeof(hc));
    hc.chroot_dir      = NULL;   /* bizd 需访问 DB / UDS，不做 chroot */
    hc.drop_uid        = -1;         /* TODO: 生产部署改为专用非特权用户 bizd 的 uid */
    hc.seccomp_profile = 2;      /* bizd：仅记录，daemon_harden 不据此装沙箱 */
    if (daemon_harden(&hc) != 0)
    {
        QLOG_FATAL(g_bizd.log, "降权/沙箱生效失败");
        goto out_fd;
    }

    QLOG_INFO(g_bizd.log, "进入事件循环");
    /* seccomp 沙箱：资源就绪后、事件循环前安装。bizd 多线程 + Unix-Socket IPC +
       下游 TCP 转发；实现位于共享库 libqcommon 的 daemon_seccomp_apply，profile=2=bizd。
       注：bizd profile 尚未 strace 画像，默认不安装；置 BIZD_SECCOMP=log|kill
       才真正装载，先 log 采集被拦 syscall 再切 kill（fail-closed）。 */
    (void)daemon_seccomp_apply(SECCOMP_PROFILE_BIZD);

    while (!daemon_should_stop())
    {
        (void)q_reactor_run(g_bizd.reactor, g_bcfg.poll_ms);
    }

    QLOG_INFO(g_bizd.log, "bizd 退出");
    rc = 0;

out_fd:
    if (g_bizd.listen_fd >= 0)
        q_ipc_close(g_bizd.listen_fd, g_bcfg.ipc_path);   /* 监听端负责 unlink */
out_lock:
    pthread_mutex_destroy(&g_bizd.lock);
out_pool:
    if (g_bizd.pool)
        q_tpool_destroy(g_bizd.pool);     /* 等待在途请求处理完 */
out_reactor:
    if (g_bizd.reactor)
        q_reactor_destroy(g_bizd.reactor);
out_log:
    if (g_bizd.log)
        q_log_close(g_bizd.log);
    return rc;
}
