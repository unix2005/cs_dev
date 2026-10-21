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
    fprintf(stderr, "用法: %s [-p <端口>] [-u <用户名>] [-k <密码>] [-S <salt hex>] "
                    "[-K <pwd_hash hex>] [-i <iter>] [-O <otp>] [-h]\n", argv0);
    fprintf(stderr, "  默认: 端口=%d 用户=%s iter=%u\n",
            LOGIN_PORT_DEF, LOGIN_USER_DEF, LOGIN_PBKDF2_ITER_DEF);
    fprintf(stderr, "  -S  账号 salt（%d 字节的 hex，%d 字符）；缺省则启动时随机生成并打印\n",
            LOGIN_SALT_LEN, LOGIN_SALT_LEN * 2);
    fprintf(stderr, "  -K  期望 PBKDF2-SM3(口令,salt,iter)（%d 字节的 hex，%d 字符）；"
                    "缺省由 -k 口令现算\n", LOGIN_PWDHASH_LEN, LOGIN_PWDHASH_LEN * 2);
    fprintf(stderr, "  -O  期望动态口令（6 位等）；缺省不校验 OTP\n");
    fprintf(stderr, "  优先级: 命令行 > 环境(LOGIN_PORT/LOGIN_USER/LOGIN_PASS/LOGIN_SALT/"
                    "LOGIN_PWD_HASH/LOGIN_PBKDF2_ITER/LOGIN_OTP) > 内置默认\n");
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
    g_lcfg.pbkdf2_iter    = LOGIN_PBKDF2_ITER_DEF;
    g_lcfg.hash_mode      = 1;
    g_lcfg.log_file[0]    = '\0';   /* 默认仅控制台；避免 /var/log 权限问题 */
}

static void apply_env(void)
{
    const char *e;
    if ((e = getenv("LOGIN_PORT")) && *e) g_lcfg.port = atoi(e);
    if ((e = getenv("LOGIN_USER")) && *e) snprintf(g_lcfg.user, sizeof(g_lcfg.user), "%s", e);
    if ((e = getenv("LOGIN_PASS")) && *e) snprintf(g_lcfg.pass, sizeof(g_lcfg.pass), "%s", e);
    if ((e = getenv("LOGIN_SALT")) && *e) snprintf(g_lcfg.salt_hex, sizeof(g_lcfg.salt_hex), "%s", e);
    if ((e = getenv("LOGIN_PWD_HASH")) && *e)
        snprintf(g_lcfg.pwd_hash_hex, sizeof(g_lcfg.pwd_hash_hex), "%s", e);
    if ((e = getenv("LOGIN_PBKDF2_ITER")) && *e) g_lcfg.pbkdf2_iter = (uint32_t)strtoul(e, NULL, 10);
    if ((e = getenv("LOGIN_OTP")) && *e) snprintf(g_lcfg.otp, sizeof(g_lcfg.otp), "%s", e);
}

/*
 * 推导运行期期望凭据：salt + expect_hash。
 * 必须在使用 q_log 之后、监听端口之前调用（需要日志输出 salt 便于联调）。
 */
int login_cred_init(void)
{
    char   hex[LOGIN_SALT_LEN * 2u + 1u];
    char   hhex[LOGIN_PWDHASH_LEN * 2u + 1u];

    if (g_lcfg.pbkdf2_iter == 0u)
        g_lcfg.pbkdf2_iter = LOGIN_PBKDF2_ITER_DEF;

    /* ---- salt ---- */
    if (g_lcfg.salt_hex[0])
    {
        if (login_hex_decode(g_lcfg.salt_hex, g_lcfg.salt, LOGIN_SALT_LEN) != (int)LOGIN_SALT_LEN)
        {
            fprintf(stderr, "[login] salt_hex 非法（需 %d 个 hex 字符）\n", LOGIN_SALT_LEN * 2);
            return -1;
        }
    }
    else if (q_rand_bytes(g_lcfg.salt, LOGIN_SALT_LEN) != 0)
    {
        fprintf(stderr, "[login] salt 随机生成失败\n");
        return -1;
    }
    /* login_hex_encode 成功返回字符数、失败返回 -1：失败时降级为占位文本（不致命） */
    if (login_hex_encode(g_lcfg.salt, LOGIN_SALT_LEN, hex, sizeof(hex)) < 0)
        snprintf(hex, sizeof(hex), "(encode-failed)");

    /* ---- 期望 pwd_hash ---- */
    if (g_lcfg.pwd_hash_hex[0])
    {
        if (login_hex_decode(g_lcfg.pwd_hash_hex, g_lcfg.expect_hash, LOGIN_PWDHASH_LEN)
            != (int)LOGIN_PWDHASH_LEN)
        {
            fprintf(stderr, "[login] pwd_hash_hex 非法（需 %d 个 hex 字符）\n", LOGIN_PWDHASH_LEN * 2);
            return -1;
        }
        g_lcfg.hash_mode = 1;
    }
    else if (q_crypto_pbkdf2_sm3((const uint8_t *)g_lcfg.pass, strlen(g_lcfg.pass),
                                 g_lcfg.salt, LOGIN_SALT_LEN,
                                 g_lcfg.pbkdf2_iter, g_lcfg.expect_hash,
                                 LOGIN_PWDHASH_LEN) != 0)
    {
        /* 国密库不可用：回退遗留明文比较（仅测试用途，明确告警） */
        g_lcfg.hash_mode = 0;
        QLOG_WARN(g_login.log, "PBKDF2-SM3 不可用，回退【明文口令】比较（禁止用于生产）");
    }
    else
    {
        g_lcfg.hash_mode = 1;
    }

    if (login_hex_encode(g_lcfg.expect_hash, LOGIN_PWDHASH_LEN, hhex, sizeof(hhex)) < 0)
        snprintf(hhex, sizeof(hhex), "(encode-failed)");
    QLOG_INFO(g_login.log, "凭据就绪: user=%s salt=%s iter=%u mode=%s expect_pwd_hash=%s",
              g_lcfg.user, hex, (unsigned)g_lcfg.pbkdf2_iter,
              g_lcfg.hash_mode ? "pbkdf2-sm3" : "legacy-plaintext", hhex);
    fprintf(stderr, "[login] user=%s salt=%s iter=%u pwd_hash=%s\n",
            g_lcfg.user, hex, (unsigned)g_lcfg.pbkdf2_iter, hhex);
    return 0;
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
        else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc)
            snprintf(g_lcfg.salt_hex, sizeof(g_lcfg.salt_hex), "%s", argv[++i]);
        else if (strcmp(argv[i], "-K") == 0 && i + 1 < argc)
            snprintf(g_lcfg.pwd_hash_hex, sizeof(g_lcfg.pwd_hash_hex), "%s", argv[++i]);
        else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc)
            g_lcfg.pbkdf2_iter = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "-O") == 0 && i + 1 < argc)
            snprintf(g_lcfg.otp, sizeof(g_lcfg.otp), "%s", argv[++i]);
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

    /* 凭据派生必须在日志就绪后、监听前完成：客户端 SALT_QUERY 依赖此处确定的 salt */
    if (login_cred_init() != 0)
    {
        fprintf(stderr, "[login] 凭据初始化失败\n");
        q_log_close(g_login.log);
        return 1;
    }

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
