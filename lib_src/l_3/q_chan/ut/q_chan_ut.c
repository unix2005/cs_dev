/**
 * @file q_chan_ut.c
 * @brief q_chan 单元测试：服务端/客户端经 socketpair 完成握手、加密收发、通道绑定一致性
 *
 * 说明：依赖 Tongsuo（q_crypto）与 SM2/SM4-GCM，需在 Linux 目标平台编译运行；
 *       macOS 因无 Tongsuo 无法链接，仅作静态/逻辑审查。
 */
#include "headers.h"
#include "q_chan.h"
#include "q_crypto.h"
#include <pthread.h>
#include <sys/socket.h>

static int g_sfd = -1;
static q_chan_t *g_server = NULL;

static void *srv_thread(void *arg)
{
    (void)arg;
    if (q_chan_handshake(g_server, g_sfd) != 0)
        return (void *)-1;
    uint8_t plain[256];
    size_t plen = 0;
    uint16_t cmd = 0;
    if (q_chan_receive(g_server, g_sfd, plain, sizeof(plain), &plen, &cmd) != 0)
        return (void *)-1;
    if (plen != 5 || memcmp(plain, "hello", 5) != 0)
        return (void *)-1;
    return NULL;
}

int main(void)
{
    uint8_t *s_priv = NULL, *s_pub = NULL;
    size_t s_plen = 0, s_ulen = 0;
    if (q_crypto_sm2_keygen(&s_priv, &s_plen, &s_pub, &s_ulen) != 0)
    {
        printf("FAIL: sm2 keygen\n");
        return 1;
    }

    g_server = q_chan_create(Q_CHAN_MODE_SERVER, s_priv, s_plen, s_pub, s_ulen, NULL, 0, 1, 0);
    if (!g_server)
    {
        printf("FAIL: server create\n");
        return 1;
    }
    q_chan_t *client = q_chan_create(Q_CHAN_MODE_CLIENT, NULL, 0, NULL, 0, s_pub, s_ulen, 1, 0);
    if (!client)
    {
        printf("FAIL: client create\n");
        return 1;
    }

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
    {
        printf("FAIL: socketpair\n");
        return 1;
    }
    g_sfd = fds[1];
    int cfd = fds[0];

    pthread_t tid;
    if (pthread_create(&tid, NULL, srv_thread, NULL) != 0)
    {
        printf("FAIL: pthread_create\n");
        return 1;
    }
    if (q_chan_handshake(client, cfd) != 0)
    {
        printf("FAIL: client handshake\n");
        return 1;
    }
    if (pthread_join(tid, NULL) != 0)
    {
        printf("FAIL: pthread_join\n");
        return 1;
    }

    if (q_chan_transmit(client, cfd, (const uint8_t *)"hello", 5, 0x10) != 0)
    {
        printf("FAIL: transmit\n");
        return 1;
    }

    /* 通道绑定一致性：双方应得到相同的绑定材料 */
    uint8_t b1[32], b2[32];
    if (q_chan_binding(client, b1) != 0 || q_chan_binding(g_server, b2) != 0)
    {
        printf("FAIL: binding\n");
        return 1;
    }
    if (memcmp(b1, b2, 32) != 0)
    {
        printf("FAIL: binding mismatch\n");
        return 1;
    }

    printf("q_chan_ut: PASS\n");
    q_chan_destroy(client);
    q_chan_destroy(g_server);
    free(s_priv);
    free(s_pub);
    close(cfd);
    close(g_sfd);
    return 0;
}
