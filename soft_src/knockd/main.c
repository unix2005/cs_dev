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
 *   # netd 的公网 TCP 监听规则仅允许 ip saddr @allow4 放行
 *
 * 注意：本文件依赖 epoll（Linux），仅能在 Linux 构建与运行；
 *       macOS 等非 Linux 平台 q_reactor_create 返回 NULL，编译/运行均不可用。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "q_common.h"
#include "q_reactor.h"
#include "q_spa.h"

#define KNOCKD_PORT      5001
#define SPA_KEY_PATH     "/etc/gtkvms/spa.key"
#define SPA_KEY_LEN      16          /* SM4 密钥长度 */
#define NFT_TABLE        "inet spa"
#define NFT_SET          "allow4"
#define KNOCKD_TTL_DEF  300         /* 默认开门时长（秒） */

static uint8_t      g_spa_key[SPA_KEY_LEN];
static q_reactor_t *g_reactor = NULL;
static int          g_sock    = -1;

/* 从密钥文件读取 SPA 共享密钥（默认路径可用 GTKVMS_SPA_KEY_FILE 覆盖） */
static int load_spa_key(void)
{
    const char *env  = getenv("GTKVMS_SPA_KEY_FILE");
    const char *path = (env && *env) ? env : SPA_KEY_PATH;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[knockd] 无法打开 SPA 密钥文件: %s (%s)\n", path, strerror(errno));
        return -1;
    }
    size_t rd = fread(g_spa_key, 1, SPA_KEY_LEN, f);
    fclose(f);
    if (rd != SPA_KEY_LEN) {
        fprintf(stderr, "[knockd] SPA 密钥长度不足: %s (需要 %d 字节)\n", path, SPA_KEY_LEN);
        return -1;
    }
    return 0;
}

/* 校验通过：将源 IP 加入 nftables allow set（带超时自动过期） */
static void open_door(const char *ip, unsigned ttl_s)
{
    /* ip 来自 inet_ntop，仅含数字与 '.'，无 shell 元字符，system() 安全。
       生产环境建议改用 libnftables / netlink 直连，避免 fork+sh。 */
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "nft add element %s %s { %s timeout %us }",
             NFT_TABLE, NFT_SET, ip, ttl_s);
    if (system(cmd) != 0)
        fprintf(stderr, "[knockd] nft 开门失败: %s (需 CAP_NET_ADMIN 且 %s/%s 已预创建)\n",
                ip, NFT_TABLE, NFT_SET);
}
/* UDP 可读事件回调（在 reactor 循环线程内联执行） */
static void on_knock(int fd, uint32_t events, void *ctx)
{
    (void)events;
    (void)ctx;

    struct sockaddr_in src;
    socklen_t slen = sizeof(src);
    uint8_t  buf[256];
    ssize_t  n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
    if (n <= 0)
        return;

    /* 定长报文：长度不符直接丢弃（静默） */
    if (n != (ssize_t)sizeof(q_spa_pkt_t))
        return;

    uint16_t port = 0, ttl = 0;
    if (q_spa_verify(g_spa_key, SPA_KEY_LEN, (const q_spa_pkt_t *)buf,
                    (uint64_t)time(NULL), &port, &ttl) != 0)
        return;  /* 校验失败：静默丢弃，不回包，抗扫描 */

    char ip[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip)) == NULL)
        return;

    unsigned ttl_s = ttl ? ttl : KNOCKD_TTL_DEF;
    open_door(ip, ttl_s);
    /* 成功开门不回包（SPA 设计：服务端零响应） */
}

static int create_knock_socket(int port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[knockd] socket() 失败: %s\n", strerror(errno));
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "[knockd] bind(:%d) 失败: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}
int main(void)
{
    if (daemon_install_signal_handlers() != 0) {
        fprintf(stderr, "[knockd] 信号处理器安装失败\n");
        return 1;
    }

    if (load_spa_key() != 0)
        return 1;

    const char *pe = getenv("GTKVMS_KNOCKD_PORT");
    int port = pe ? atoi(pe) : KNOCKD_PORT;

    printf("[knockd] start (pid=%d, port=%d)\n", (int)getpid(), port);

    /* 降权 / 沙箱占位（Linux 生效；chroot 不适用 knockd，需 nftables 操作） */
    static const struct daemon_harden_cfg cfg = {
        .chroot_dir      = NULL,
        .drop_uid        = -1,   /* TODO: 仍降为非 root，仅保留 CAP_NET_ADMIN */
        .seccomp_profile = 3,    /* knockd：仅 socket/recvfrom/setsockopt + nftables 所需 */
    };
    daemon_harden(&cfg);

    g_sock = create_knock_socket(port);
    if (g_sock < 0)
        return 1;

    g_reactor = q_reactor_create();
    if (!g_reactor) {
        fprintf(stderr, "[knockd] q_reactor_create 失败（仅 Linux 支持 epoll）\n");
        close(g_sock);
        return 1;
    }
    if (q_reactor_add(g_reactor, g_sock, EPOLLIN, on_knock, NULL) != 0) {
        fprintf(stderr, "[knockd] q_reactor_add 失败\n");
        q_reactor_destroy(g_reactor);
        close(g_sock);
        return 1;
    }

    /* 事件循环：收到 SIGINT/SIGTERM 后 daemon_should_stop() 置位即退出 */
    while (!daemon_should_stop())
        q_reactor_run(g_reactor, 200);

    printf("[knockd] shutting down\n");
    q_reactor_destroy(g_reactor);
    close(g_sock);
    return 0;
}
