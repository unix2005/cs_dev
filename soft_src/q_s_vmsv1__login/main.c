/*
 * login —— 登录微服务（bizd 的下游，经 TCP + JSON 与 bizd 对接）
 *
 * 职责：接收 bizd 转发的 JSON 信封，完成登录鉴权（测试默认 qiaoshui / 123456），
 *       签发 Token 并回写 JSON 响应。用于打通 客户端 -> netd -> bizd -> login 全链路。
 *
 * 文件划分（与 bizd/netd 对称）：
 *   srv_login.h      公共头（类型 / 常量 / 接口聚合）
 *   main.c           入口：参数/环境解析 + 资源装配 + 主循环 + 清理
 *   login_util.c     大端序列化 / hex 编解码 / 定长帧收发
 *   login_handler.c  信封解析 + 登录鉴权 + 响应组装
 *   login_srv.c      TCP 监听 / accept 回调 / 每连接 worker
 */
#include "srv_login.h"

login_cfg_t      g_lcfg;
struct login_ctx g_login;

static void usage(const char *argv0)
{
    fprintf(stderr, "用法: %s [-p <端口>] [-u <用户名>] [-k <密码>] [-h]\n", argv0);
    fprintf(stderr, "  默认: 端口=%d 用户=%s\n", LOGIN_PORT_DEF, LOGIN_USER_DEF);
    fprintf(stderr, "  优先级: 命令行 > 环境(LOGIN_PORT/LOGIN_USER/LOGIN_PASS) > 内置默认\n");
}

void login_cfg_defaults(void)
{
    memset(&g_lcfg, 0, sizeof(g_lcfg));
    g_lcfg.port           = LOGIN_PORT_DEF;
    g_lcfg.backlog        = LOGIN_BACKLOG_DEF;
    g_lcfg.nworkers       = LOGIN_NWORKERS_DEF;
    g_lcfg.poll_ms        = LOGIN_POLL_MS_DEF;
    g_lcfg.io_timeout_sec = LOGIN_IO_TIMEOUT_SEC;
    g_lcfg.log_level      = (int)Q_LOG_INFO;
    g_lcfg.log_console    = 1;
    g_lcfg.log_max_size   = 10u * 1024u * 1024u;
    snprintf(g_lcfg.user, sizeof(g_lcfg.user), "%s", LOGIN_USER_DEF);
    snprintf(g_lcfg.pass, sizeof(g_lcfg.pass), "%s", LOGIN_PASS_DEF);
    g_lcfg.log_file[0]    = '\0';   /* 默认仅控制台；避免 /var/log 权限问题 */
}

static void apply_env(void)
{
    const char *e;
    if ((e = getenv("LOGIN_PORT")) && *e) g_lcfg.port = atoi(e);
    if ((e = getenv("LOGIN_USER")) && *e) snprintf(g_lcfg.user, sizeof(g_lcfg.user), "%s", e);
    if ((e = getenv("LOGIN_PASS")) && *e) snprintf(g_lcfg.pass, sizeof(g_lcfg.pass), "%s", e);
}

int main(int argc, char **argv)
{
    int i, rc = 1;
    struct daemon_harden_cfg hc;

    login_cfg_defaults();
    apply_env();

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            g_lcfg.port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc)
            snprintf(g_lcfg.user, sizeof(g_lcfg.user), "%s", argv[++i]);
        else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc)
            snprintf(g_lcfg.pass, sizeof(g_lcfg.pass), "%s", argv[++i]);
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    memset(&g_login, 0, sizeof(g_login));
    g_login.listen_fd = (q_fd_t)-1;

    g_login.log = q_log_open(g_lcfg.log_file[0] ? g_lcfg.log_file : NULL,
                             (q_log_level_t)g_lcfg.log_level, g_lcfg.log_max_size);
    if (!g_login.log)
    {
        fprintf(stderr, "[login] 日志初始化失败\n");
        return 1;
    }
    q_log_set_console(g_login.log, g_lcfg.log_console);

    if (daemon_install_signal_handlers() != 0)
        QLOG_WARN(g_login.log, "信号处理器安装失败（将无法优雅退出）");

    QLOG_INFO(g_login.log, "login 启动 (pid=%d, port=%d, user=%s, workers=%d)",
              (int)getpid(), g_lcfg.port, g_lcfg.user, g_lcfg.nworkers);

    g_login.reactor = q_reactor_create();
    if (!g_login.reactor) { QLOG_FATAL(g_login.log, "reactor 创建失败"); goto out_log; }

    g_login.pool = q_tpool_create(g_lcfg.nworkers, LOGIN_BACKLOG_DEF * 16);
    if (!g_login.pool) { QLOG_FATAL(g_login.log, "线程池创建失败"); goto out_reactor; }

    if (q_net_bind_listen(g_lcfg.port, g_lcfg.backlog, &g_login.listen_fd) != 0)
    { QLOG_FATAL(g_login.log, "监听 TCP %d 失败", g_lcfg.port); goto out_pool; }

    if (q_reactor_add(g_login.reactor, g_login.listen_fd, EPOLLIN,
                      login_on_accept, &g_login) != 0)
    { QLOG_FATAL(g_login.log, "监听 fd 注册 reactor 失败"); goto out_fd; }

    /* 降权：测试服务不做 chroot/降权；无专用 seccomp profile，暂不装沙箱 */
    memset(&hc, 0, sizeof(hc));
    hc.chroot_dir      = NULL;
    hc.drop_uid        = -1;
    hc.seccomp_profile = 0;
    if (daemon_harden(&hc) != 0)
    { QLOG_FATAL(g_login.log, "降权生效失败"); goto out_fd; }

    QLOG_INFO(g_login.log, "进入事件循环");
    while (!daemon_should_stop())
        (void)q_reactor_run(g_login.reactor, g_lcfg.poll_ms);

    QLOG_INFO(g_login.log, "login 退出");
    rc = 0;

out_fd:
    if (g_login.listen_fd != (q_fd_t)-1)
        q_net_close(g_login.listen_fd);
out_pool:
    if (g_login.pool)
        q_tpool_destroy(g_login.pool);
out_reactor:
    if (g_login.reactor)
        q_reactor_destroy(g_login.reactor);
out_log:
    if (g_login.log)
        q_log_close(g_login.log);
    return rc;
}
