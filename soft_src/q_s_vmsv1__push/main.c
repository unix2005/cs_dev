/**
 * @file main.c
 * @brief 实时推送微服务（q_service_push / pushd）入口：配置 -> UDS 监听 -> reactor + 模拟线程
 *
 * M5：推送栈（knockd_B + netd_B + pushd）的下游微服务，与 q_s_vmsv1__biz 平级。
 *       与 biz 的根本差异在连接模型：bizd 一问一答，pushd 持久会话 + 反向推送
 *       （netd_B 为每条推送客户端连接持一条 UDS，见 srv_push.h 文件头）。
 *
 * 用法：
 *   q_service_push [-s <socket 路径>] [--vehicles N] [--tick-ms N] [--emit-ms N]
 *                  [--no-alarms] [--no-token] [-l <日志文件>] [-L <日志级别>]
 *                  [--selftest] [-h]
 *   默认: sock=/var/run/gtkvms/pushd.sock，6 台测试车，步长 100ms，上报间隔 1s
 *   优先级: 命令行 > 环境(PUSH_SOCK/PUSH_VEHICLES/PUSH_TICK_MS/PUSH_EMIT_MS/
 *             PUSH_NO_ALARMS/PUSH_NO_TOKEN) > 内置默认
 */
#include "srv_push.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

push_cfg_t      g_pcfg;   /* 全局配置（srv_push.h 声明 extern） */
struct push_ctx g_push;   /* 全局运行期对象 */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "用法: %s [-s <socket 路径>] [--vehicles N] [--tick-ms N] [--emit-ms N]\n"
            "       [--no-alarms] [--no-token] [-l <日志>] [-L <级别>] [--selftest] [-h]\n",
            argv0);
    fprintf(stderr, "  默认: sock=%s 车辆=%u 步长=%ums 上报=%ums 报警=开 校验token=开\n",
            PUSH_SOCK_DEF, (unsigned)PUSH_VEH_MAX, (unsigned)PUSH_TICK_MS_DEF,
            (unsigned)PUSH_EMIT_MS_DEF);
    fprintf(stderr, "  优先级: 命令行 > 环境(PUSH_SOCK/PUSH_VEHICLES/PUSH_TICK_MS/"
                    "PUSH_EMIT_MS/PUSH_NO_ALARMS/PUSH_NO_TOKEN) > 内置默认\n");
}

void push_cfg_defaults(void)
{
    memset(&g_pcfg, 0, sizeof(g_pcfg));
    snprintf(g_pcfg.sock_path, sizeof(g_pcfg.sock_path), "%s", PUSH_SOCK_DEF);
    g_pcfg.backlog        = PUSH_BACKLOG_DEF;
    g_pcfg.nworkers       = PUSH_NWORKERS_DEF;
    g_pcfg.poll_ms        = PUSH_POLL_MS_DEF;
    g_pcfg.io_timeout_sec = PUSH_IO_TIMEOUT_SEC;
    g_pcfg.vehicles       = PUSH_VEH_MAX;
    g_pcfg.tick_ms        = PUSH_TICK_MS_DEF;
    g_pcfg.emit_ms        = PUSH_EMIT_MS_DEF;
    g_pcfg.no_alarms      = 0;
    g_pcfg.require_token  = 1;
    g_pcfg.log_level      = (int)Q_LOG_INFO;
    g_pcfg.log_console    = 1;
    g_pcfg.log_max_size   = 10u * 1024u * 1024u;
    g_pcfg.log_file[0]    = '\0';   /* 默认仅控制台；避免 /var/log 权限问题 */
}

static unsigned long env_u32(const char *name, unsigned long def)
{
    const char *e = getenv(name);
    return (e && *e) ? strtoul(e, NULL, 10) : def;
}

static void apply_env(void)
{
    const char *e;

    if ((e = getenv("PUSH_SOCK")) && *e)
        snprintf(g_pcfg.sock_path, sizeof(g_pcfg.sock_path), "%s", e);
    if ((e = getenv("PUSH_VEHICLES")) && *e)
        g_pcfg.vehicles = (uint32_t)env_u32("PUSH_VEHICLES", PUSH_VEH_MAX);
    if ((e = getenv("PUSH_TICK_MS")) && *e)
        g_pcfg.tick_ms = (uint32_t)env_u32("PUSH_TICK_MS", PUSH_TICK_MS_DEF);
    if ((e = getenv("PUSH_EMIT_MS")) && *e)
        g_pcfg.emit_ms = (uint32_t)env_u32("PUSH_EMIT_MS", PUSH_EMIT_MS_DEF);
    if (getenv("PUSH_NO_ALARMS"))
        g_pcfg.no_alarms = 1;
    if (getenv("PUSH_NO_TOKEN"))
        g_pcfg.require_token = 0;
}

int main(int argc, char *argv[])
{
    struct daemon_harden_cfg hc;
    pthread_t   sim_tid;
    int         sim_on = 0;
    int         i, rc = 1;

    push_cfg_defaults();
    apply_env();

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--selftest") == 0)
        {
            return push_selftest();
        }
        else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sock") == 0) && i + 1 < argc)
            snprintf(g_pcfg.sock_path, sizeof(g_pcfg.sock_path), "%s", argv[++i]);
        else if (strcmp(argv[i], "--vehicles") == 0 && i + 1 < argc)
            g_pcfg.vehicles = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--tick-ms") == 0 && i + 1 < argc)
            g_pcfg.tick_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--emit-ms") == 0 && i + 1 < argc)
            g_pcfg.emit_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--no-alarms") == 0)
            g_pcfg.no_alarms = 1;
        else if (strcmp(argv[i], "--no-token") == 0)
            g_pcfg.require_token = 0;
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
            snprintf(g_pcfg.log_file, sizeof(g_pcfg.log_file), "%s", argv[++i]);
        else if (strcmp(argv[i], "-L") == 0 && i + 1 < argc)
            g_pcfg.log_level = atoi(argv[++i]);
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    /* 参数兜底：车辆数超表容量按容量走；步长 0 会让模拟线程忙循环，保底 10ms */
    if (g_pcfg.vehicles == 0u || g_pcfg.vehicles > PUSH_VEH_MAX)
        g_pcfg.vehicles = PUSH_VEH_MAX;
    if (g_pcfg.tick_ms < 10u)
        g_pcfg.tick_ms = 10u;
    if (g_pcfg.emit_ms < 100u)
        g_pcfg.emit_ms = 100u;

    memset(&g_push, 0, sizeof(g_push));
    g_push.listen_fd = (q_fd_t)-1;

    g_push.log = q_log_open(g_pcfg.log_file[0] ? g_pcfg.log_file : NULL,
                            (q_log_level_t)g_pcfg.log_level, g_pcfg.log_max_size);
    if (!g_push.log)
    {
        fprintf(stderr, "[push] 日志初始化失败\n");
        return 1;
    }
    q_log_set_console(g_push.log, g_pcfg.log_console);

    if (daemon_install_signal_handlers() != 0)
        QLOG_WARN(g_push.log, "信号处理器安装失败（将无法优雅退出）");

    QLOG_INFO(g_push.log, "pushd 启动 (pid=%d, sock=%s, workers=%d, 车辆=%u, 步长=%ums, 上报=%ums, 报警=%s, 校验token=%d)",
              (int)getpid(), g_pcfg.sock_path, g_pcfg.nworkers,
              (unsigned)g_pcfg.vehicles, (unsigned)g_pcfg.tick_ms, (unsigned)g_pcfg.emit_ms,
              g_pcfg.no_alarms ? "关" : "开", g_pcfg.require_token);

    g_push.reactor = q_reactor_create();
    if (!g_push.reactor) { QLOG_FATAL(g_push.log, "reactor 创建失败"); goto out_log; }

    g_push.pool = q_tpool_create(g_pcfg.nworkers, PUSH_BACKLOG_DEF * 16);
    if (!g_push.pool) { QLOG_FATAL(g_push.log, "线程池创建失败"); goto out_reactor; }

    /* UDS 监听（q_ipc_listen 会先 unlink 残留路径，便于重启）；
       监听 fd 置非阻塞：reactor 电平触发下 on_accept 才能把待接受连接一次取空。 */
    if (q_ipc_listen(g_pcfg.sock_path, g_pcfg.backlog, &g_push.listen_fd) != 0)
    { QLOG_FATAL(g_push.log, "监听 UDS %s 失败", g_pcfg.sock_path); goto out_pool; }
    if (q_net_set_nonblock(g_push.listen_fd) != 0)
        QLOG_WARN(g_push.log, "监听 fd 置非阻塞失败（accept 可能空转）");

    if (q_reactor_add(g_push.reactor, g_push.listen_fd, EPOLLIN, push_on_accept, &g_push) != 0)
    { QLOG_FATAL(g_push.log, "监听 fd 注册 reactor 失败"); goto out_fd; }

    /* 模拟线程：数据源 + 发送者（先起线程再进循环，保证第一帧数据尽快产出） */
    if (push_sim_start(&sim_tid) != 0)
    {
        QLOG_WARN(g_push.log, "模拟线程启动失败：无轨迹/报警数据下发（会话仍可建立）");
    }
    else
    {
        sim_on = 1;
    }

    /* 降权：测试服务不做 chroot/降权；无专用 seccomp profile，暂不装沙箱 */
    memset(&hc, 0, sizeof(hc));
    hc.chroot_dir      = NULL;
    hc.drop_uid        = -1;
    hc.seccomp_profile = 0;
    if (daemon_harden(&hc) != 0)
    { QLOG_FATAL(g_push.log, "降权生效失败"); goto out_fd; }

    QLOG_INFO(g_push.log, "进入事件循环");
    while (!daemon_should_stop())
        (void)q_reactor_run(g_push.reactor, g_pcfg.poll_ms);

    QLOG_INFO(g_push.log, "pushd 退出");
    if (sim_on)
        push_sim_join(sim_tid);
    rc = 0;

out_fd:
    if (g_push.listen_fd != (q_fd_t)-1)
        q_ipc_close(g_push.listen_fd, g_pcfg.sock_path);   /* 监听端：退出时清理 socket 文件 */
out_pool:
    if (g_push.pool)
        q_tpool_destroy(g_push.pool);
out_reactor:
    if (g_push.reactor)
        q_reactor_destroy(g_push.reactor);
out_log:
    if (g_push.log)
        q_log_close(g_push.log);
    return rc;
}
