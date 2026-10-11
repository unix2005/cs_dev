/*
 * spa_send —— knockd 测试发送端（仅用于联调 / 验证开门闭环）
 *
 * 构造一个合法 SPA 单包（q_spa_build），经 UDP 单包发往 knockd 监听端口。
 * 与 knockd 共用同一把 SPA 密钥文件即可通过校验。
 *
 * 用法：
 *   spa_send [target_host] [target_port] [req_port] [req_ttl] [user_id]
 *   缺省：127.0.0.1 5001 22 300 "test-user"
 *
 * 注意：本工具仅用 UDP 套接字 + q_spa，不依赖 epoll，逻辑上可跨平台；
 *       但项目全局 LDFLAGS 含 -lxml2 -luuid（macOS 缺省无），故需与 knockd
 *       同在 Linux 构建环境编译（此处的 q_* 静态库与 Tongsuo 均已在 Linux 就绪）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "q_spa.h"

#define SPA_KEY_PATH "/etc/gtkvms/spa.key"
#define SPA_KEY_LEN  16

static int load_spa_key(uint8_t *key, size_t key_len)
{
    const char *env  = getenv("GTKVMS_SPA_KEY_FILE");
    const char *path = (env && *env) ? env : SPA_KEY_PATH;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "spa_send: 无法打开 SPA 密钥文件: %s (%s)\n", path, strerror(errno));
        return -1;
    }
    size_t rd = fread(key, 1, key_len, f);
    fclose(f);
    if (rd != key_len) {
        fprintf(stderr, "spa_send: SPA 密钥长度不足: %s (需要 %zu 字节)\n", path, key_len);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *host     = argc > 1 ? argv[1] : "127.0.0.1";
    int port             = argc > 2 ? atoi(argv[2]) : 5001;
    uint16_t req_port    = (uint16_t)(argc > 3 ? atoi(argv[3]) : 22);
    uint16_t req_ttl     = (uint16_t)(argc > 4 ? atoi(argv[4]) : 300);
    const char *user_str = argc > 5 ? argv[5] : "test-user";

    uint8_t key[SPA_KEY_LEN];
    if (load_spa_key(key, SPA_KEY_LEN) != 0)
        return 1;

    /* user_id 定长 32 字节；q_spa_build 仅将其纳入 AAD 认证，knockd 不强制内容 */
    uint8_t user_id[Q_SPA_USERID_LEN];
    memset(user_id, 0, sizeof(user_id));
    size_t ulen = strlen(user_str);
    if (ulen > Q_SPA_USERID_LEN) ulen = Q_SPA_USERID_LEN;
    memcpy(user_id, user_str, ulen);

    /* ip_hint 16 字节：knockd 采用 recvfrom 真实源 IP，此处填 0 即可 */
    uint8_t ip_hint[16];
    memset(ip_hint, 0, sizeof(ip_hint));

    q_spa_pkt_t pkt;
    if (q_spa_build(key, SPA_KEY_LEN, /*key_id*/ 0, user_id, req_port, req_ttl, ip_hint, &pkt) != 0) {
        fprintf(stderr, "spa_send: q_spa_build 失败\n");
        return 1;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "spa_send: socket() 失败: %s\n", strerror(errno));
        return 1;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        fprintf(stderr, "spa_send: 非法目标地址: %s\n", host);
        close(fd);
        return 1;
    }

    ssize_t n = sendto(fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&sa, sizeof(sa));
    if (n != (ssize_t)sizeof(pkt)) {
        fprintf(stderr, "spa_send: sendto 失败: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("spa_send: 已发送 SPA 单包 -> %s:%d (req_port=%u req_ttl=%u user=%.*s)\n",
           host, port, req_port, req_ttl, (int)ulen, user_str);
    printf("spa_send: SPA 为静默协议，服务端不回包；请在 knockd 主机用\n");
    printf("           nft list set inet spa allow4\n");
    printf("           确认源 IP 已出现（%us 后自动过期）。\n", req_ttl);

    close(fd);
    return 0;
}
