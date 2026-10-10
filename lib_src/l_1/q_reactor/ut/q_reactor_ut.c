/**
 * @file q_reactor_ut.c
 * @brief q_reactor 单元测试：reactor + 定时器、有界线程池、reactor->worker 集成
 */
#include "headers.h"
#include "q_reactor.h"

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); g_fail++; } \
                      else printf("PASS: %s\n", m); } while (0)

/* ---- 定时器 + reactor ---- */
static int g_hits = 0;
static void timer_h(int fd, uint32_t events, void *ctx)
{
    uint64_t v = 0;
    (void)events; (void)ctx;
    if (read(fd, &v, sizeof(v)) == (ssize_t)sizeof(v))
        g_hits += (int)v;
}

/* ---- 线程池 ---- */
static int g_cnt = 0;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static void inc(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_mtx);
    g_cnt++;
    pthread_mutex_unlock(&g_mtx);
}

/* ---- 集成：timer handler 内派发到 worker 池 ---- */
static q_tpool_t *g_wp = NULL;
static int g_work = 0;
static void work_fn(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_mtx);
    g_work++;
    pthread_mutex_unlock(&g_mtx);
}
static void timer_dispatch_h(int fd, uint32_t events, void *ctx)
{
    uint64_t v = 0;
    (void)events; (void)ctx;
    if (read(fd, &v, sizeof(v)) == (ssize_t)sizeof(v) && g_wp)
        q_tpool_dispatch(g_wp, work_fn, NULL);
}

int main(void)
{
    /* 1) reactor + 定时器 */
    int tfd = -1;
    CHECK(q_reactor_timer_create(&tfd) == 0, "timer create");
    CHECK(q_reactor_timer_arm(tfd, 50, 100) == 0, "timer arm 50/100");
    q_reactor_t *r = q_reactor_create();
    CHECK(r != NULL, "reactor create");
    CHECK(q_reactor_add(r, tfd, EPOLLIN, timer_h, NULL) == 0, "reactor add timer");
    q_reactor_run(r, 300);
    q_reactor_run(r, 300);
    CHECK(g_hits >= 1, "reactor timer 触发 >=1");
    q_reactor_del(r, tfd);
    close(tfd);
    q_reactor_destroy(r);

    /* 2) 有界线程池（无界） */
    g_cnt = 0;
    q_tpool_t *p = q_tpool_create(4, 0);
    CHECK(p != NULL, "tpool create");
    for (int i = 0; i < 100; i++)
        CHECK(q_tpool_dispatch(p, inc, NULL) == 0, "tpool dispatch");
    q_tpool_destroy(p);
    CHECK(g_cnt == 100, "tpool 100 任务全部执行");

    /* 3) 有界队列（上限 2，提交 5 应阻塞等待完成） */
    g_cnt = 0;
    q_tpool_t *p2 = q_tpool_create(2, 2);
    CHECK(p2 != NULL, "tpool 有界 create");
    for (int i = 0; i < 5; i++)
        CHECK(q_tpool_dispatch(p2, inc, NULL) == 0, "tpool 有界 dispatch");
    q_tpool_destroy(p2);
    CHECK(g_cnt == 5, "tpool 有界 5 任务全部执行");

    /* 4) 集成：reactor 事件 -> 派发到 worker 池 */
    g_work = 0;
    int tfd2 = -1;
    q_reactor_timer_create(&tfd2);
    q_reactor_timer_arm(tfd2, 30, 60);
    q_reactor_t *r2 = q_reactor_create();
    q_tpool_t *wp = q_tpool_create(2, 0);
    g_wp = wp;
    q_reactor_add(r2, tfd2, EPOLLIN, timer_dispatch_h, NULL);
    q_reactor_run(r2, 300);
    q_reactor_run(r2, 300);
    g_wp = NULL;
    q_reactor_del(r2, tfd2);
    close(tfd2);
    q_reactor_destroy(r2);
    q_tpool_destroy(wp);   /* join worker：确保所有派发任务执行完毕后再断言 */
    CHECK(g_work >= 1, "reactor->worker 派发 >=1");

    if (g_fail == 0) printf("\nALL PASS\n");
    else printf("\n%d FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
