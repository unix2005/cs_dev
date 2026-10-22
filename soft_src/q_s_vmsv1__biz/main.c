/**
 * @file main.c
 * @brief 业务微服务（q_service_biz）入口：配置解析 -> 监听 -> reactor 事件循环
 *
 * M2：bizd 的 ep_biz(9003) 下游桩服务，支撑 客户端 -> netd -> bizd -> biz 全链路联调。
 *
 * 用法：
 *   q_service_biz [-p <端口>] [--total N] [--online N] [--offline N]
 *                 [--excluded N] [--alarm N] [--vtotal N] [--vonline N] [--page-max N]
 *                 [--no-token] [-l <日志文件>] [-L <日志级别>] [-h]
 *   默认: 端口=9003，统计基线=34554/10846/23708/0/46343，车辆桩 34554 台 / 在线 10846 台
 *   优先级: 命令行 > 环境(BIZ_PORT/BIZ_TOTAL/...) > 内置默认
 */
#include "srv_biz.h"

biz_cfg_t      g_bcfg;   /* 全局配置（srv_biz.h 声明 extern） */
struct biz_ctx g_biz;    /* 全局运行期对象 */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "用法: %s [-p <端口>] [--total N] [--online N] [--offline N]\n"
            "       [--excluded N] [--alarm N] [--vtotal N] [--vonline N] [--page-max N]\n"
            "       [--no-token] [-l <日志>] [-L <级别>] [-h]\n",
            argv0);
    fprintf(stderr, "  默认: 端口=%d 总数=%u 在线=%u 报警=%u 车辆桩=%u/%u 单页上限=%u\n",
            BIZ_PORT_DEF, BIZ_TOTAL_DEF, BIZ_ONLINE_DEF, BIZ_ALARM_UNHANDLED_DEF,
            BIZ_VTOTAL_DEF, BIZ_VONLINE_DEF, BIZ_PAGE_LIMIT_MAX);
    fprintf(stderr, "  优先级: 命令行 > 环境(BIZ_PORT/BIZ_TOTAL/.../BIZ_NO_TOKEN) > 内置默认\n");
}

void biz_cfg_defaults(void)
{
    memset(&g_bcfg, 0, sizeof(g_bcfg));
    g_bcfg.port            = BIZ_PORT_DEF;
    g_bcfg.backlog         = BIZ_BACKLOG_DEF;
    g_bcfg.nworkers        = BIZ_NWORKERS_DEF;
    g_bcfg.poll_ms         = BIZ_POLL_MS_DEF;
    g_bcfg.io_timeout_sec  = BIZ_IO_TIMEOUT_SEC;
    g_bcfg.total           = BIZ_TOTAL_DEF;
    g_bcfg.online          = BIZ_ONLINE_DEF;
    g_bcfg.offline         = BIZ_OFFLINE_DEF;
    g_bcfg.excluded        = BIZ_EXCLUDED_DEF;
    g_bcfg.alarm_unhandled = BIZ_ALARM_UNHANDLED_DEF;
    /* M3：d_vehicleinfo / d_org_info 桩规模 */
    g_bcfg.vehicle_total   = BIZ_VTOTAL_DEF;
    g_bcfg.vehicle_online  = BIZ_VONLINE_DEF;
    g_bcfg.page_limit_max  = BIZ_PAGE_LIMIT_MAX;
    g_bcfg.require_token   = 1;
    g_bcfg.log_level       = (int)Q_LOG_INFO;
    g_bcfg.log_console     = 1;
    g_bcfg.log_max_size    = 10u * 1024u * 1024u;
    g_bcfg.log_file[0]     = '\0';   /* 默认仅控制台；避免 /var/log 权限问题 */
}

static unsigned long env_u32(const char *name, unsigned long def)
{
    const char *e = getenv(name);
    return (e && *e) ? strtoul(e, NULL, 10) : def;
}

static void apply_env(void)
{
    if (getenv("BIZ_PORT"))      g_bcfg.port      = (int)env_u32("BIZ_PORT", (unsigned long)BIZ_PORT_DEF);
    if (getenv("BIZ_TOTAL"))     g_bcfg.total     = (uint32_t)env_u32("BIZ_TOTAL", BIZ_TOTAL_DEF);
    if (getenv("BIZ_ONLINE"))    g_bcfg.online    = (uint32_t)env_u32("BIZ_ONLINE", BIZ_ONLINE_DEF);
    if (getenv("BIZ_OFFLINE"))   g_bcfg.offline   = (uint32_t)env_u32("BIZ_OFFLINE", BIZ_OFFLINE_DEF);
    if (getenv("BIZ_EXCLUDED"))  g_bcfg.excluded  = (uint32_t)env_u32("BIZ_EXCLUDED", BIZ_EXCLUDED_DEF);
    if (getenv("BIZ_ALARM"))     g_bcfg.alarm_unhandled = (uint32_t)env_u32("BIZ_ALARM", BIZ_ALARM_UNHANDLED_DEF);
    if (getenv("BIZ_VTOTAL"))    g_bcfg.vehicle_total   = (uint32_t)env_u32("BIZ_VTOTAL", BIZ_VTOTAL_DEF);
    if (getenv("BIZ_VONLINE"))   g_bcfg.vehicle_online  = (uint32_t)env_u32("BIZ_VONLINE", BIZ_VONLINE_DEF);
    if (getenv("BIZ_PAGE_MAX"))  g_bcfg.page_limit_max  = (uint32_t)env_u32("BIZ_PAGE_MAX", BIZ_PAGE_LIMIT_MAX);
    if (getenv("BIZ_NO_TOKEN"))  g_bcfg.require_token   = 0;
}

int main(int argc, char *argv[])
{
    struct daemon_harden_cfg hc;
    int   i, rc = 1;

    biz_cfg_defaults();
    apply_env();

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            g_bcfg.port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--total") == 0 && i + 1 < argc)
            g_bcfg.total = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--online") == 0 && i + 1 < argc)
            g_bcfg.online = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--offline") == 0 && i + 1 < argc)
            g_bcfg.offline = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--excluded") == 0 && i + 1 < argc)
            g_bcfg.excluded = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--alarm") == 0 && i + 1 < argc)
            g_bcfg.alarm_unhandled = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--vtotal") == 0 && i + 1 < argc)
            g_bcfg.vehicle_total = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--vonline") == 0 && i + 1 < argc)
            g_bcfg.vehicle_online = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--page-max") == 0 && i + 1 < argc)
            g_bcfg.page_limit_max = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--no-token") == 0)
            g_bcfg.require_token = 0;
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
            snprintf(g_bcfg.log_file, sizeof(g_bcfg.log_file), "%s", argv[++i]);
        else if (strcmp(argv[i], "-L") == 0 && i + 1 < argc)
            g_bcfg.log_level = atoi(argv[++i]);
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    memset(&g_biz, 0, sizeof(g_biz));
    g_biz.listen_fd = (q_fd_t)-1;

    g_biz.log = q_log_open(g_bcfg.log_file[0] ? g_bcfg.log_file : NULL,
                           (q_log_level_t)g_bcfg.log_level, g_bcfg.log_max_size);
    if (!g_biz.log)
    {
        fprintf(stderr, "[biz] 日志初始化失败\n");
        return 1;
    }
    q_log_set_console(g_biz.log, g_bcfg.log_console);

    if (daemon_install_signal_handlers() != 0)
        QLOG_WARN(g_biz.log, "信号处理器安装失败（将无法优雅退出）");

    QLOG_INFO(g_biz.log, "biz 启动 (pid=%d, port=%d, workers=%d, 统计 %u/%u/%u, 报警 %u, 车辆桩 %u/%u, 单页上限 %u, 校验token=%d)",
              (int)getpid(), g_bcfg.port, g_bcfg.nworkers,
              g_bcfg.total, g_bcfg.online, g_bcfg.offline,
              g_bcfg.alarm_unhandled, g_bcfg.vehicle_total, g_bcfg.vehicle_online,
              g_bcfg.page_limit_max, g_bcfg.require_token);

    g_biz.reactor = q_reactor_create();
    if (!g_biz.reactor) { QLOG_FATAL(g_biz.log, "reactor 创建失败"); goto out_log; }

    g_biz.pool = q_tpool_create(g_bcfg.nworkers, BIZ_BACKLOG_DEF * 16);
    if (!g_biz.pool) { QLOG_FATAL(g_biz.log, "线程池创建失败"); goto out_reactor; }

    if (q_net_bind_listen(g_bcfg.port, g_bcfg.backlog, &g_biz.listen_fd) != 0)
    { QLOG_FATAL(g_biz.log, "监听 TCP %d 失败", g_bcfg.port); goto out_pool; }

    if (q_reactor_add(g_biz.reactor, g_biz.listen_fd, EPOLLIN, biz_on_accept, &g_biz) != 0)
    { QLOG_FATAL(g_biz.log, "监听 fd 注册 reactor 失败"); goto out_fd; }

    /* 降权：测试服务不做 chroot/降权；无专用 seccomp profile，暂不装沙箱 */
    memset(&hc, 0, sizeof(hc));
    hc.chroot_dir      = NULL;
    hc.drop_uid        = -1;
    hc.seccomp_profile = 0;
    if (daemon_harden(&hc) != 0)
    { QLOG_FATAL(g_biz.log, "降权生效失败"); goto out_fd; }

    QLOG_INFO(g_biz.log, "进入事件循环");
    while (!daemon_should_stop())
        (void)q_reactor_run(g_biz.reactor, g_bcfg.poll_ms);

    QLOG_INFO(g_biz.log, "biz 退出");
    rc = 0;

out_fd:
    if (g_biz.listen_fd != (q_fd_t)-1)
        q_net_close(g_biz.listen_fd);
out_pool:
    if (g_biz.pool)
        q_tpool_destroy(g_biz.pool);
out_reactor:
    if (g_biz.reactor)
        q_reactor_destroy(g_biz.reactor);
out_log:
    if (g_biz.log)
        q_log_close(g_biz.log);
    return rc;
}
