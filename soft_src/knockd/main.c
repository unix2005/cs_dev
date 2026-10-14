/*
 * knockd —— SPA 单包授权进程（UDP，仅 CAP_NET_ADMIN）
 *
 * 真实逻辑（相对骨架）：
 *   - 创建 UDP 监听套接字，绑定固定单端口（默认 5001）
 *   - 经 q_reactor（epoll 事件循环）监听该 fd 的可读事件
 *   - 每收到一个 UDP 包：先按定长截取，再 q_spa_verify 做 SM4-GCM 验签 +
 *     时间戳 ±30s 校验（全部在库内恒定时间完成）；失败则【静默丢弃、不回包】
 *   - 校验通过：取 recvfrom 的【实际源 IP】（不信任报文内 ip_hint，防 IP 欺骗），
 *     用 nft 命令将其加入本机 nftables allow set（TTL 取报文 req_ttl，缺省 300s）
 *   - SPA 共享密钥从本地密钥文件读取（默认 /etc/gtkvms/spa.key，16 字节 = SM4 密钥长）
 *
 * 安全基线：
 *   - 仅授予 CAP_NET_ADMIN（部署时用 AmbientCapabilities，不给完整 root）
 *   - 进程应运行在非特权用户下，由 daemon_harden 占位收口（TODO）
 *   - 未认证 UDP 解析器是首个接触攻击者的代码，须配 seccomp 严格收敛 syscall，
 *     并纳入 AFL++/libFuzzer 7x24 零崩溃验证
 *
 * 部署前置（运维侧）：
 *   nft add table inet spa
 *   nft add set inet spa allow4 { type ipv4_addr; timeout; }
 *   nft add set inet spa allow6 { type ipv6_addr; timeout; }
 *   # netd 的公网 TCP 监听规则仅允许 ip saddr @allow4 / ip6 saddr @allow6 放行
 *   # 开门动作经 libnftables（netlink）直连内核，无需安装 nft 命令行工具
 *
 * 注意：本文件依赖 epoll（Linux），仅能在 Linux 构建与运行；
 *       macOS 等非 Linux 平台 q_reactor_create 返回 NULL，编译/运行均不可用。
 */
#include "srv_knockd.h"

static knockd_cfg_t g_cfg;
static q_log_t *g_log = NULL;   /* 全局日志句柄（q_log_open 失败亦回退控制台，非空） */
static char g_audit_prev[65] = "0"; /* 审计链上一条哈希(hex)；首条 prev="0" */

static uint8_t g_spa_key[SPA_KEY_LEN];
static q_reactor_t *g_reactor = NULL;
static int g_sock = -1;       /* IPv4 监听 fd */
static int g_sock6 = -1;      /* IPv6 监听 fd（-1 表示未启用） */
static struct nft_ctx *g_nft = NULL;  /* libnftables 上下文（懒初始化，进程退出时释放） */
static knock_ctx_t g_ctx4 = { NULL };
static knock_ctx_t g_ctx6 = { NULL };


/* 日志级别字符串 -> 枚举（配置值约定小写） */
static q_log_level_t parse_log_level(const char *s)
{
    if (!s || !*s)
        return Q_LOG_INFO;
    if (strcmp(s, "trace") == 0)
        return Q_LOG_TRACE;
    if (strcmp(s, "debug") == 0)
        return Q_LOG_DEBUG;
    if (strcmp(s, "info") == 0)
        return Q_LOG_INFO;
    if (strcmp(s, "warn") == 0)
        return Q_LOG_WARN;
    if (strcmp(s, "error") == 0)
        return Q_LOG_ERROR;
    if (strcmp(s, "fatal") == 0)
        return Q_LOG_FATAL;
    if (strcmp(s, "audit") == 0)
        return Q_LOG_AUDIT;
    return Q_LOG_INFO;
}

/* SPA 报文 user_id(32B) 转 hex（供审计；user_id 本身已是 SM3 哈希，非明文） */
static const char *spa_uid_hex(const uint8_t uid[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
    {
        out[2 * i]     = hex[uid[i] >> 4];
        out[2 * i + 1] = hex[uid[i] & 0xf];
    }
    out[64] = '\0';
    return out;
}

/* 从密钥文件读取 SPA 共享密钥（路径由调用方决定，已含环境变量覆盖） */
static int load_spa_key(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "[knockd] 无法打开 SPA 密钥文件: %s (%s)\n", path, strerror(errno));
        return -1;
    }
    size_t rd = fread(g_spa_key, 1, SPA_KEY_LEN, f);
    fclose(f);
    if (rd != SPA_KEY_LEN)
    {
        fprintf(stderr, "[knockd] SPA 密钥长度不足: %s (需要 %d 字节)\n", path, SPA_KEY_LEN);
        return -1;
    }
    return 0;
}

/* 载入 XML 配置（q_xcfg）；键缺失时回退到上方 #define 默认值。
   键路径示例：config.knockd.listen_port */
static int load_config(const char *path)
{
    q_xcfg_ctx_t *xc = q_xcfg_init(path);
    if (!xc)
    {
        fprintf(stderr, "[knockd] 无法解析配置文件: %s\n", path);
        return -1;
    }

    g_cfg.listen_port     = q_xcfg_get_int(xc, "config.knockd.listen_port", KNOCKD_PORT);
    g_cfg.default_ttl     = q_xcfg_get_int(xc, "config.knockd.default_ttl", KNOCKD_TTL_DEF);
    g_cfg.poll_timeout_ms = q_xcfg_get_int(xc, "config.knockd.poll_timeout_ms", KNOCKD_POLL_DEF);

    char *v;
    v = q_xcfg_get_string(xc, "config.knockd.spa_key_file", SPA_KEY_PATH);
    if (v) { snprintf(g_cfg.spa_key_file, sizeof(g_cfg.spa_key_file), "%s", v); free(v); }
    v = q_xcfg_get_string(xc, "config.knockd.nft_table", NFT_TABLE);
    if (v) { snprintf(g_cfg.nft_table, sizeof(g_cfg.nft_table), "%s", v); free(v); }
    v = q_xcfg_get_string(xc, "config.knockd.nft_set", NFT_SET);
    if (v) { snprintf(g_cfg.nft_set, sizeof(g_cfg.nft_set), "%s", v); free(v); }
    v = q_xcfg_get_string(xc, "config.knockd.nft_set6", NFT_SET6);
    if (v) { snprintf(g_cfg.nft_set6, sizeof(g_cfg.nft_set6), "%s", v); free(v); }

    v = q_xcfg_get_string(xc, "config.knockd.log_file", "");
    if (v) { snprintf(g_cfg.log_file, sizeof(g_cfg.log_file), "%s", v); free(v); }
    v = q_xcfg_get_string(xc, "config.knockd.log_level", "info");
    if (v) { snprintf(g_cfg.log_level, sizeof(g_cfg.log_level), "%s", v); free(v); }
    g_cfg.log_console  = q_xcfg_get_bool(xc, "config.knockd.log_console", 1);
    g_cfg.log_max_size = q_xcfg_get_int(xc, "config.knockd.log_max_size", 10 * 1024 * 1024);

    q_xcfg_destroy(xc);
    return 0;
}

/* 校验通过：将源 IP 加入对应 nftables 放行集合（带 timeout 自动过期）。
   直连 libnftables（netlink），不经过 /bin/sh，从结构上杜绝命令注入、
   省去 fork+sh 开销，且能拿到结构化返回码。set 由调用方按地址族传入
   （v4->allow4 / v6->allow6）。 */
static void open_door(const char *set, const char *ip, unsigned ttl_s)
{
    if (!g_nft)
        g_nft = nft_ctx_new();
    if (!g_nft)
    {
        QLOG_ERROR(g_log, "nftables 上下文创建失败（系统缺少 libq.../libnftables.so.1）");
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "add element %s %s { %s timeout %us }",
             g_cfg.nft_table, set, ip, ttl_s);
    if (nft_run_cmd_from_buffer(g_nft, buf) != 0)
        QLOG_ERROR(g_log, "nft 开门失败: %s (需 CAP_NET_ADMIN 且 %s/%s 已预创建)",
                   ip, g_cfg.nft_table, set);
}
/* UDP 可读事件回调（在 reactor 循环线程内联执行）。
   ctx 指向该监听套接字对应的 knock_ctx_t（含应写入的 nft 集合名，按地址族区分）。 */
static void on_knock(int fd, uint32_t events, void *ctx)
{
    (void)events;
    const knock_ctx_t *kc = (const knock_ctx_t *)ctx;

    struct sockaddr_storage src;
    socklen_t slen = sizeof(src);
    uint8_t buf[256];
    ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
    if (n <= 0)
        return;

    /* 定长报文：长度不符直接丢弃（静默） */
    if (n != (ssize_t)sizeof(q_spa_pkt_t))
        return;

    /* 源端点（v4/v6 统一）：
       - ep（"ip:port"，端口转本机序）供日志/审计，与本项目其它守护进程一致，
         由通用 q_net_addr_ntop 格式化；
       - ip_only（纯 IP）专供 nft 放行集合元素（集合类型为 ipv4_addr/ipv6_addr，不含端口）。
       地址取自 recvfrom 的真实源（不信任报文内 ip_hint，防 IP 欺骗）。 */
    char ep[INET6_ADDRSTRLEN + 8];
    char ip_only[INET6_ADDRSTRLEN];
    const void *ap;
    uint16_t sport_host;
    if (src.ss_family == AF_INET)
    {
        const struct sockaddr_in *a4 = (const struct sockaddr_in *)&src;
        inet_ntop(AF_INET, &a4->sin_addr, ip_only, sizeof(ip_only));
        ap = &a4->sin_addr;
        sport_host = ntohs(a4->sin_port);
    }
    else if (src.ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)&src;
        inet_ntop(AF_INET6, &a6->sin6_addr, ip_only, sizeof(ip_only));
        ap = &a6->sin6_addr;
        sport_host = ntohs(a6->sin6_port);
    }
    else
    {
        return;  /* 非 IP 地址族，忽略 */
    }
    q_net_addr_ntop((int)src.ss_family, ap, sport_host, ep, sizeof(ep));

    uint16_t port = 0, ttl = 0;
    if (q_spa_verify(g_spa_key, SPA_KEY_LEN, (const q_spa_pkt_t *)buf, (uint64_t)time(NULL), &port, &ttl) != 0)
    {
        /* 校验失败：网络层静默丢弃、不回包（抗扫描）；
           本地仅记 DEBUG，避免被扫描/伪造流量刷爆日志（需排障时把 log_level 调为 debug） */
        QLOG_DEBUG(g_log, "SPA 校验失败 from=%s len=%zd", ep, n);
        return;
    }

    unsigned ttl_s = ttl ? ttl : KNOCKD_TTL_DEF;
    open_door(kc->set, ip_only, ttl_s);

    /* 成功授权是安全关键事件，必须留审计痕迹（始终记录，不受级别过滤） */
    char uidhex[65];
    char amsg[160];
    char chain[512];
    uint8_t dig[32];
    char cur_hex[65];
    spa_uid_hex(((const q_spa_pkt_t *)buf)->user_id, uidhex);
    snprintf(amsg, sizeof(amsg), "remote=%s ttl=%u uid=%s", ep, ttl_s, uidhex);

    /* SM3 链式哈希（防篡改）：cur = SM3(prev || "SPA" || "open_ok" || msg) */
    int cl = snprintf(chain, sizeof(chain), "%s|SPA|open_ok|%s", g_audit_prev, amsg);
    if (q_crypto_sm3((const uint8_t *)chain, (size_t)cl, dig) == 0)
    {
        spa_uid_hex(dig, cur_hex);
        q_log_audit(g_log, "SPA", "open_ok", amsg, g_audit_prev, cur_hex);
        memcpy(g_audit_prev, cur_hex, sizeof(cur_hex));
    }
    else
    {
        /* SM3 异常也照常记审计，仅缺链式哈希 */
        q_log_audit(g_log, "SPA", "open_ok", amsg, g_audit_prev, NULL);
    }
    QLOG_INFO(g_log, "SPA 授权成功: %s", amsg);
    /* 成功开门不回包（SPA 设计：服务端零响应） */
}

static int create_knock_socket(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        fprintf(stderr, "[knockd] socket() 失败: %s\n", strerror(errno));
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
        fprintf(stderr, "[knockd] bind(:%d) 失败: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* 创建 IPv6 UDP 监听套接字（[::]:port，强制 IPV6_V6ONLY 避免与 v4 共享导致
   地址族歧义：v4 映射地址无法干净地归入 ipv4_addr 型集合）。失败返回 -1。 */
static int create_knock_socket6(int port)
{
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        fprintf(stderr, "[knockd] socket6() 失败: %s\n", strerror(errno));
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));

    struct sockaddr_in6 sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin6_family = AF_INET6;
    sa.sin6_addr   = in6addr_any;
    sa.sin6_port   = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
        fprintf(stderr, "[knockd] bind6(:%d) 失败: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}


int main(int argc, char *argv[])
{
    /* 启动参数解析：knockd [-c <config.xml>]
       配置路径优先级：argv -c  >  环境变量 GTKVMS_KNOCKD_CONF  >  默认值 */
    const char *conf_path = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            conf_path = argv[++i];
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            printf("usage: %s [-c <config.xml>]\n", argv[0]);
            printf("  -c <file>  XML 配置文件路径（默认 %s）\n", KNOCKD_CONF_PATH);
            return 0;
        }
    }
    if (!conf_path)
        conf_path = getenv("GTKVMS_KNOCKD_CONF");
    if (!conf_path)
        conf_path = KNOCKD_CONF_PATH;

    if (daemon_install_signal_handlers() != 0)
    {
        fprintf(stderr, "[knockd] 信号处理器安装失败\n");
        return 1;
    }

    /* 载入配置 */
    if (load_config(conf_path) != 0)
        return 1;

    /* 打开日志：log_file 为空=仅控制台；文件打开失败自动回退控制台。
       q_log_open 恒返回非空句柄，可安全解引用。 */
    g_log = q_log_open(g_cfg.log_file[0] ? g_cfg.log_file : NULL,
                       parse_log_level(g_cfg.log_level), (uint64_t)g_cfg.log_max_size);
    q_log_set_console(g_log, g_cfg.log_console);

    /* 运行期填充每栈上下文指向的集合名（v4 -> allow4 / v6 -> allow6） */
    g_ctx4.set = g_cfg.nft_set;
    g_ctx6.set = g_cfg.nft_set6;

    QLOG_INFO(g_log, "config: port=%d ttl=%d poll=%d key=%s nft=%s/%s|%s log=%s level=%s console=%d",
              g_cfg.listen_port, g_cfg.default_ttl, g_cfg.poll_timeout_ms,
              g_cfg.spa_key_file, g_cfg.nft_table, g_cfg.nft_set, g_cfg.nft_set6,
              g_cfg.log_file[0] ? g_cfg.log_file : "(console)", g_cfg.log_level, g_cfg.log_console);

    /* 环境变量优先于配置文件（便于运维临时覆盖） */
    const char *ke = getenv("GTKVMS_SPA_KEY_FILE");
    const char *key_path = (ke && *ke) ? ke : g_cfg.spa_key_file;
    if (load_spa_key(key_path) != 0)
        return 1;

    const char *pe = getenv("GTKVMS_KNOCKD_PORT");
    int port = pe ? atoi(pe) : g_cfg.listen_port;

    QLOG_INFO(g_log, "knockd start pid=%d port=%d", (int)getpid(), port);

    /* 降权占位（Linux 生效；chroot 不适用 knockd，需 nftables 操作）。
       注意：daemon_harden 当前仅做 chroot/setuid（均为 no-op），seccomp 沙箱
       见下方 daemon_seccomp_apply(SECCOMP_PROFILE_KNOCKD)，由共享库 libqcommon
       提供，在资源就绪后、事件循环前安装。 */
    static const struct daemon_harden_cfg hc = {
        .chroot_dir = NULL,
        .drop_uid = -1,              /* TODO: 仍降为非 root，仅保留 CAP_NET_ADMIN */
        .seccomp_profile = SECCOMP_PROFILE_KNOCKD, /* 仅记录，daemon_harden 不据此装沙箱 */
    };
    daemon_harden(&hc);

    g_sock = create_knock_socket(port);
    if (g_sock < 0)
    {
        QLOG_ERROR(g_log, "create_knock_socket 失败");
        return 1;
    }

    g_reactor = q_reactor_create();
    if (!g_reactor)
    {
        QLOG_ERROR(g_log, "q_reactor_create 失败（仅 Linux 支持 epoll）");
        close(g_sock);
        return 1;
    }
    if (q_reactor_add(g_reactor, g_sock, EPOLLIN, on_knock, &g_ctx4) != 0)
    {
        QLOG_ERROR(g_log, "q_reactor_add 失败");
        q_reactor_destroy(g_reactor);
        close(g_sock);
        return 1;
    }

    /* IPv6 监听（尽力启用；不可用则降级为仅 IPv4，不致命） */
    g_sock6 = create_knock_socket6(port);
    if (g_sock6 >= 0)
    {
        if (q_reactor_add(g_reactor, g_sock6, EPOLLIN, on_knock, &g_ctx6) != 0)
        {
            QLOG_ERROR(g_log, "q_reactor_add(v6) 失败");
            close(g_sock6);
            g_sock6 = -1;
        }
        else
            QLOG_INFO(g_log, "knockd IPv6 监听已启用 port=%d set=%s", port, g_cfg.nft_set6);
    }
    else
    {
        QLOG_WARN(g_log, "IPv6 套接字不可用，仅监听 IPv4");
    }

    /* 进入事件循环前安装 seccomp 沙箱（此时 socket/reactor/log 均已就绪；
       首包才会懒加载 libnftables 上下文走 Netlink，已在允许列表内）。
       沙箱实现位于共享库 libqcommon（daemon_seccomp_apply，profile=3=knockd）。
       安装失败仅记日志、不阻断服务（纵深防御失败也应尽量可用）。 */
    (void)daemon_seccomp_apply(SECCOMP_PROFILE_KNOCKD);

    /* 事件循环：收到 SIGINT/SIGTERM 后 daemon_should_stop() 置位即退出 */
    while (!daemon_should_stop())
        q_reactor_run(g_reactor, g_cfg.poll_timeout_ms);

    QLOG_INFO(g_log, "knockd shutting down");
    q_reactor_destroy(g_reactor);
    close(g_sock);
    if (g_sock6 >= 0)
        close(g_sock6);
    if (g_nft)
        nft_ctx_free(g_nft);
    q_log_close(g_log);
    return 0;
}
