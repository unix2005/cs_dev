/**
 * @file push_srv.c
 * @brief 推送微服务服务器：UDS 监听 accept + 线程池每会话 worker + 会话表
 *
 * 与 biz_srv.c 的差异：
 *   - biz  ：bizd 每请求新建 TCP 连接，一问一答后关闭 —— worker 是"一次性"的；
 *   - pushd：netd_B 为每条推送连接**持久** UDS —— worker 是"会话级"的，
 *             阻塞 recv 直到对端关闭；期间模拟线程持续向同一条 fd 写 TRAJ/ALARM。
 *
 * 锁模型（文件头 srv_push.h 有完整说明）：
 *   - g_lock 保护会话表 + 各会话订阅集合，并串行化所有 socket 写；
 *   - worker 退出：先摘链表（持锁）再关 fd —— 模拟线程整趟发送都持锁，
 *     故不存在"边发边关"；关 fd 只在 worker 单线程进行，无 use-after-close。
 */
#include "srv_push.h"

#include <stdlib.h>
#include <string.h>

q_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   /* 见文件头锁模型 */

/* ------------------------------ 会话表 ------------------------------ */

static push_session_t *g_sessions = NULL;   /* 链表头；g_lock 保护 */

int push_session_add(push_session_t *s)
{
    push_session_t *cur;

    if (!s)
        return -1;
    for (cur = g_sessions; cur; cur = cur->next)
        if (cur == s)
            return 0;                       /* 已在表内：幂等 */
    s->next = g_sessions;
    g_sessions = s;
    return 0;
}

void push_session_remove(push_session_t *s)
{
    push_session_t **pp = &g_sessions;

    if (!s)
        return;
    while (*pp)
    {
        if (*pp == s)
        {
            *pp = s->next;
            s->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

push_session_t *push_session_first(void)
{
    return g_sessions;
}

/* --------------------------- 订阅集合维护 --------------------------- */

int push_sub_add(push_session_t *s, const char *vid)
{
    uint32_t i;

    if (!s || !vid || !vid[0])
        return -1;
    for (i = 0u; i < s->nvid; ++i)
        if (strcmp(s->vids[i], vid) == 0)
            return 0;                       /* 已在集合：幂等 */
    if (s->nvid >= PUSH_SUBS_MAX)
        return -1;                          /* 上限保护：防配置错误吃光内存 */
    if (s->nvid >= s->cap_vid)
    {
        uint32_t ncap = s->cap_vid ? s->cap_vid * 2u : 64u;
        char (*nv)[PUSH_ID_MAX];

        if (ncap > PUSH_SUBS_MAX)
            ncap = PUSH_SUBS_MAX;
        nv = (char (*)[PUSH_ID_MAX])realloc(s->vids, (size_t)ncap * PUSH_ID_MAX);
        if (!nv)
            return -1;
        s->vids = nv;
        s->cap_vid = ncap;
    }
    memset(s->vids[s->nvid], 0, PUSH_ID_MAX);
    snprintf(s->vids[s->nvid], PUSH_ID_MAX, "%s", vid);
    ++s->nvid;
    return 0;
}

void push_sub_remove(push_session_t *s, const char *vid)
{
    uint32_t i;

    if (!s || !vid)
        return;
    for (i = 0u; i < s->nvid; ++i)
    {
        if (strcmp(s->vids[i], vid) != 0)
            continue;
        if (i != s->nvid - 1u)
            memcpy(s->vids[i], s->vids[s->nvid - 1u], PUSH_ID_MAX);
        --s->nvid;
        return;
    }
}

/* ------------------------------ worker ------------------------------ */

void push_conn_worker(void *arg)
{
    push_conn_t     *c = (push_conn_t *)arg;
    push_session_t  *s;
    uint8_t         *buf;

    s = (push_session_t *)calloc(1, sizeof(*s));
    buf = (uint8_t *)malloc(PUSH_MAXMSG);
    if (!s || !buf)
        goto cleanup;

    s->fd = c->fd;

    /* 收发超时：RCVTIMEO 让"对端静默"可被循环感知（否则异常对端会永久占用 worker），
       但**超时不拆链** —— SUB_DELTA 免回执，netd_B 订完默背后 UDS 可长时间无帧，
       10s 没数据是正常形态（M5 联调时误判为断连，客户端每 10s 重连一次）。
       SNDTIMEO 同时兜住模拟线程的推送发送（UI 不读时 5s 判死，防整链停滞）。 */
    (void)push_set_io_timeout(c->fd, g_pcfg.io_timeout_sec);

    for (;;)
    {
        size_t len = 0u;
        int    rc;

        rc = push_recv_frame(c->fd, buf, PUSH_MAXMSG, &len);
        if (rc == 1)
            continue;                  /* 超时：订阅为空时 UDS 长静默是常态，续等 */
        if (rc != 0)
            break;                     /* EOF / 错误：对端已走，拆链 */

        pthread_mutex_lock(&g_lock);
        rc = push_handle_frame(s, buf, len);
        pthread_mutex_unlock(&g_lock);
        if (rc < 0)
            break;
    }

cleanup:
    pthread_mutex_lock(&g_lock);
    push_session_remove(s);                 /* 先摘链表：模拟线程此后看不到本会话 */
    pthread_mutex_unlock(&g_lock);

    if (s)
    {
        if (s->vids)
            free(s->vids);
        free(s);
    }
    free(buf);
    q_ipc_close(c->fd, NULL);      /* UDS 会话 fd：不传 path（unlink 只属监听端） */
    free(c);
}

/* ------------------------------ accept ------------------------------ */

void push_on_accept(q_fd_t fd, uint32_t events, void *ctx)
{
    struct push_ctx *g = (struct push_ctx *)ctx;
    (void)events;

    for (;;)
    {
        int          cfd = -1;
        push_conn_t *c;

        if (q_ipc_accept(fd, &cfd) != 0)
            break;                          /* 暂无待接受连接（电平触发会再次回调） */

        c = (push_conn_t *)malloc(sizeof(*c));
        if (!c)
        {
            q_net_close((q_fd_t)cfd);
            continue;
        }
        c->fd = (q_fd_t)cfd;

        if (q_tpool_dispatch(g->pool, push_conn_worker, c) != 0)
        {
            QLOG_WARN(g->log, "线程池队列满，拒绝推送会话连接");
            q_net_close(c->fd);
            free(c);
            break;                          /* 队列满：停止本轮 accept 避免雪崩 */
        }
    }
}
