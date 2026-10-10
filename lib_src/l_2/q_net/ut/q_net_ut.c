/**
 * @file q_net_ut.c
 * @brief q_net 单元测试：socket、IPv4/IPv6 UDP、地址解析、客户端阻塞收发、
 *        Reactor + Worker 服务器（Reactor 读取后派发 Worker 处理）
 */
#include "headers.h"
#include "q_net.h"

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); g_fail++; } \
                      else printf("PASS: %s\n", m); } while (0)

/* ===== Reactor + Worker 服务器：Worker 收到数据 ===== */
static int g_got = 0;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static char g_recvbuf[256];
static size_t g_reclen = 0;

static void on_data(int fd, void *buf, size_t len, void *ctx)
{
    (void)ctx;
    pthread_mutex_lock(&g_mtx);
    g_reclen = len < sizeof(g_recvbuf) ? len : sizeof(g_recvbuf);
    memcpy(g_recvbuf, buf, g_reclen);
    g_got = 1;
    pthread_mutex_unlock(&g_mtx);
    (void)q_net_send_blocking(fd, "ok", 2);   /* Worker 内回写响应 */
}

static void *srv_thread(void *arg)
{
    q_net_srv_t *s = (q_net_srv_t *)arg;
    q_net_srv_run(s);
    return NULL;
}

int main(void)
{
    /* ===== socket 基础 ===== */
    int s = -1;
    CHECK(q_net_socket_tcp(&s) == 0, "socket tcp");
    CHECK(s >= 0, "socket fd");
    CHECK(q_net_set_nonblock(s) == 0, "set nonblock");
    q_net_close(s);

    int lf = -1;
    CHECK(q_net_bind_listen(18080, 8, &lf) == 0, "bind_listen 18080");
    q_net_close(lf);

    /* ===== IPv4 UDP 回环 ===== */
    int usrv = -1, ucli = -1;
    CHECK(q_net_bind(18085, &usrv) == 0, "udp bind 18085");
    CHECK(q_net_socket_udp(&ucli) == 0, "udp socket");
    struct sockaddr_storage dst; socklen_t dstlen;
    CHECK(q_net_addr_resolve("127.0.0.1", 18085, &dst, &dstlen) == 0, "udp resolve 127.0.0.1");
    const char *umsg = "udp4-hello";
    CHECK(q_net_udp_send(ucli, umsg, strlen(umsg), (struct sockaddr *)&dst, dstlen) == (ssize_t)strlen(umsg), "udp4 send");
    char ubuf[128];
    struct sockaddr_storage from; socklen_t fromlen = sizeof(from);
    ssize_t ur = q_net_udp_recv(usrv, ubuf, sizeof(ubuf) - 1, (struct sockaddr *)&from, &fromlen);
    CHECK(ur == (ssize_t)strlen(umsg) && memcmp(ubuf, umsg, ur) == 0, "udp4 recv 内容一致");
    q_net_close(ucli); q_net_close(usrv);

    /* ===== IPv6 UDP 回环（::1） ===== */
    int usrv6 = -1, ucli6 = -1;
    CHECK(q_net_bind6(18086, &usrv6) == 0, "udp6 bind6 18086");
    CHECK(q_net_socket_udp6(&ucli6) == 0, "udp6 socket");
    struct sockaddr_storage dst6; socklen_t dst6len;
    CHECK(q_net_addr_resolve("::1", 18086, &dst6, &dst6len) == 0, "udp6 resolve ::1");
    const char *umsg6 = "udp6-hello";
    CHECK(q_net_udp_send(ucli6, umsg6, strlen(umsg6), (struct sockaddr *)&dst6, dst6len) == (ssize_t)strlen(umsg6), "udp6 send");
    char ubuf6[128];
    struct sockaddr_storage from6; socklen_t from6len = sizeof(from6);
    ssize_t ur6 = q_net_udp_recv(usrv6, ubuf6, sizeof(ubuf6) - 1, (struct sockaddr *)&from6, &from6len);
    CHECK(ur6 == (ssize_t)strlen(umsg6) && memcmp(ubuf6, umsg6, ur6) == 0, "udp6 recv 内容一致");
    q_net_close(ucli6); q_net_close(usrv6);

    /* ===== 地址解析 / 通配 ===== */
    struct sockaddr_storage ss; socklen_t slen;
    CHECK(q_net_addr_resolve("127.0.0.1", 1234, &ss, &slen) == 0, "addr_resolve v4");
    CHECK(ss.ss_family == AF_INET, "addr_resolve v4 family");
    CHECK(q_net_addr_resolve("::1", 1234, &ss, &slen) == 0, "addr_resolve v6");
    CHECK(ss.ss_family == AF_INET6, "addr_resolve v6 family");
    CHECK(q_net_addr_resolve("not_an_ip", 1234, &ss, &slen) == -1, "addr_resolve 非法 -> -1");
    CHECK(q_net_addr_any(0, 9999, &ss, &slen) == 0, "addr_any v4");
    CHECK(ss.ss_family == AF_INET, "addr_any v4 family");
    CHECK(q_net_addr_any(1, 9999, &ss, &slen) == 0, "addr_any v6");
    CHECK(ss.ss_family == AF_INET6, "addr_any v6 family");

    /* ===== TCP 阻塞收发回环 ===== */
    int tlf = -1, tcf = -1, taf = -1;
    CHECK(q_net_bind_listen(18087, 8, &tlf) == 0, "tcp bind_listen 18087");
    CHECK(q_net_connect("127.0.0.1", 18087, &tcf) == 0, "tcp connect 127.0.0.1");
    CHECK(q_net_accept(tlf, &taf) == 0, "tcp accept");
    const char *tmsg = "blocking-tcp";
    CHECK(q_net_send_blocking(tcf, tmsg, strlen(tmsg)) == (ssize_t)strlen(tmsg), "tcp send_blocking");
    char tbuf[64];
    ssize_t tr = q_net_recv_blocking(taf, tbuf, strlen(tmsg));
    CHECK(tr == (ssize_t)strlen(tmsg) && memcmp(tbuf, tmsg, tr) == 0, "tcp recv_blocking 内容一致");
    q_net_close(tcf); q_net_close(taf); q_net_close(tlf);

    /* ===== IPv6 TCP 连接/接受/阻塞收发 ===== */
    int tlf6 = -1, tcf6 = -1, taf6 = -1;
    CHECK(q_net_bind_listen6(18088, 8, &tlf6) == 0, "tcp6 bind_listen6 18088");
    CHECK(q_net_connect6("::1", 18088, &tcf6) == 0, "tcp6 connect6 ::1");
    CHECK(q_net_accept(tlf6, &taf6) == 0, "tcp6 accept");
    uint8_t pb[4] = {1, 2, 3, 4};
    CHECK(q_net_send_blocking(tcf6, pb, 4) == 4, "tcp6 send_blocking");
    uint8_t rb[4];
    ssize_t r6 = q_net_recv_blocking(taf6, rb, 4);
    CHECK(r6 == 4 && memcmp(pb, rb, 4) == 0, "tcp6 recv_blocking 内容一致");
    q_net_close(tcf6); q_net_close(taf6); q_net_close(tlf6);

    /* ===== Reactor + Worker 服务器（Reactor 读满 -> Worker 处理） ===== */
    g_got = 0;
    q_net_srv_t *srv = q_net_srv_create(4);
    CHECK(srv != NULL, "srv create");
    q_net_srv_set_handler(srv, on_data, NULL);
    CHECK(q_net_srv_listen(srv, 18099, 8) == 0, "srv listen 18099");
    pthread_t th;
    CHECK(pthread_create(&th, NULL, srv_thread, srv) == 0, "srv thread start");
    int cf = -1;
    CHECK(q_net_connect("127.0.0.1", 18099, &cf) == 0, "srv client connect");
    const char *smsg = "hello-reactor-worker";
    CHECK(q_net_send_blocking(cf, smsg, strlen(smsg)) == (ssize_t)strlen(smsg), "srv client send");
    for (int i = 0; i < 200 && !g_got; i++)
        usleep(1000);
    CHECK(g_got == 1, "srv worker 收到数据");
    CHECK(g_reclen == strlen(smsg) && memcmp(g_recvbuf, smsg, g_reclen) == 0, "srv 数据内容一致");
    char rsp[8];
    ssize_t rr = q_net_recv_blocking(cf, rsp, 2);
    CHECK(rr == 2 && memcmp(rsp, "ok", 2) == 0, "srv Worker 回写响应");
    q_net_close(cf);
    q_net_srv_stop(srv);
    pthread_join(th, NULL);
    q_net_srv_destroy(srv);

    if (g_fail == 0) printf("\nALL PASS\n");
    else printf("\n%d FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
