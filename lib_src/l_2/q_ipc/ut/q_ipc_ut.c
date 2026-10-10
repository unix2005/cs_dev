/**
 * @file q_ipc_ut.c
 * @brief q_ipc 单元测试：UDS 回环收发
 */
#include "headers.h"
#include "q_ipc.h"

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); g_fail++; } \
                      else printf("PASS: %s\n", m); } while (0)

int main(void)
{
    const char *path = "/tmp/q_ipc_ut.sock";
    unlink(path);

    int lfd = -1, cfd = -1, afd = -1;
    CHECK(q_ipc_listen(path, 8, &lfd) == 0, "ipc listen");
    CHECK(lfd >= 0, "ipc listen fd");

    CHECK(q_ipc_connect(path, &cfd) == 0, "ipc connect");
    CHECK(cfd >= 0, "ipc connect fd");

    CHECK(q_ipc_accept(lfd, &afd) == 0, "ipc accept");
    CHECK(afd >= 0, "ipc accept fd");

    const char *msg = "hello-ipc";
    ssize_t w = q_ipc_send(cfd, msg, strlen(msg));
    CHECK(w == (ssize_t)strlen(msg), "ipc send");

    char buf[64];
    ssize_t r = q_ipc_recv(afd, buf, sizeof(buf) - 1);
    CHECK(r == (ssize_t)strlen(msg), "ipc recv len");
    buf[r] = '\0';
    CHECK(strcmp(buf, msg) == 0, "ipc 内容一致");

    CHECK(q_ipc_send(afd, msg, strlen(msg)) == (ssize_t)strlen(msg), "ipc 回写");
    r = q_ipc_recv(cfd, buf, sizeof(buf) - 1);
    buf[r] = '\0';
    CHECK(strcmp(buf, msg) == 0, "ipc 回写内容一致");

    q_ipc_close(cfd, NULL);
    q_ipc_close(afd, NULL);
    q_ipc_close(lfd, path);

    if (g_fail == 0) printf("\nALL PASS\n");
    else printf("\n%d FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
