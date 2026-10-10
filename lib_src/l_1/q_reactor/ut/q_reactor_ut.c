/**
 * @file q_reactor_ut.c
 * @brief q_reactor 单元测试：通用任务分发核心 + 兼容旧 reactor/timer/线程池
 *
 * 设计目标验证：
 *   A) 通用任务分发核心 q_disp：submit 到 worker 池并行执行（任意线程、与 fd/IO 无关）
 *   B) 跨线程 submit：非 reactor 线程也能把任务派发进核心
 *   C) 本地任务 submit_local：非 fd 回调在事件循环线程内联执行（reactor 也能调度非 IO 任务）
 *   D) 兼容旧行为：reactor + 定时器、有界线程池、reactor->worker 集成（仅 Linux）
 */
#include "headers.h"
#include "q_reactor.h"
#include <pthread.h>

static int g_fail = 0;
#define CHECK(c, m)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            printf("FAIL: %s\n", m);                                                                                   \
            g_fail++;                                                                                                  \
        }                                                                                                              \
        else                                                                                                           \
            printf("PASS: %s\n", m);                                                                                   \
    } while (0)

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

/* ---- A) 通用分发核心：worker 池并行执行 ---- */
static int g_disp_cnt = 0;
static void disp_inc(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_mtx);
    g_disp_cnt++;
    pthread_mutex_unlock(&g_mtx);
}

/* ---- B) 跨线程 submit ---- */
static void *submit_thread(void *arg)
{
    q_disp_t *d = (q_disp_t *)arg;
    for (int i = 0; i < 50; i++)
        q_disp_submit(d, disp_inc, NULL);
    return NULL;
}

/* ---- C) 本地任务（在循环线程内联执行，验证“非 fd 回调被 reactor 调度”） ---- */
static int g_local_ran = 0;
static pthread_t g_local_tid = 0;
static void local_fn(void *arg)
{
    (void)arg;
    g_local_ran = 1;
    g_local_tid = pthread_self();
}

#ifdef __linux__
/* ---- D) 兼容旧行为：定时器 + reactor ---- */
static int g_hits = 0;
static void timer_h(int fd, uint32_t events, void *ctx)
{
    uint64_t v = 0;
    (void)events;
    (void)ctx;
    if (read(fd, &v, sizeof(v)) == (ssize_t)sizeof(v))
        g_hits += (int)v;
}

/* ---- 有界线程池（旧） ---- */
static int g_cnt = 0;
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
    (void)events;
    (void)ctx;
    if (read(fd, &v, sizeof(v)) == (ssize_t)sizeof(v) && g_wp)
        q_tpool_dispatch(g_wp, work_fn, NULL);
}
#endif /* __linux__ */

int main(void)
{
    /* A) 通用分发核心：submit 到 worker 池 */
    g_disp_cnt = 0;
    q_disp_t *d = q_disp_create(4);
    CHECK(d != NULL, "q_disp create(4)");
    for (int i = 0; i < 100; i++)
        CHECK(q_disp_submit(d, disp_inc, NULL) == 0, "q_disp submit");
    q_disp_destroy(d); /* join worker，确保所有任务完成后再断言 */
    CHECK(g_disp_cnt == 100, "q_disp 100 任务全部执行");

    /* B) 跨线程 submit */
    g_disp_cnt = 0;
    q_disp_t *d2 = q_disp_create(3);
    CHECK(d2 != NULL, "q_disp create(2)");
    pthread_t th;
    CHECK(pthread_create(&th, NULL, submit_thread, d2) == 0, "跨线程启动");
    pthread_join(th, NULL);
    q_disp_destroy(d2);
    CHECK(g_disp_cnt == 50, "跨线程 submit 50 任务全部执行");

    /* C) 本地任务：非 fd 回调在循环线程内联执行 */
    g_local_ran = 0;
    q_disp_t *d3 = q_disp_create(0); /* 仅事件循环，无 worker */
    CHECK(d3 != NULL, "q_disp create(0)");
    CHECK(q_disp_submit(d3, local_fn, NULL) == -1, "无 worker 时 submit 返回 -1");
    q_disp_submit_local(d3, local_fn, NULL);
    q_disp_run(d3, 500); /* 被 wake 唤醒后内联执行 local_fn */
    CHECK(g_local_ran == 1, "q_disp 本地任务（非 fd 回调）被执行");
    CHECK(pthread_equal(g_local_tid, pthread_self()) != 0, "本地任务在事件循环(本)线程内联执行");
    q_disp_destroy(d3);

#ifdef __linux__
    /* D1) reactor + 定时器（旧行为保留） */
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

    /* D2) 有界线程池（无界） */
    g_cnt = 0;
    q_tpool_t *p = q_tpool_create(4, 0);
    CHECK(p != NULL, "tpool create");
    for (int i = 0; i < 100; i++)
        CHECK(q_tpool_dispatch(p, inc, NULL) == 0, "tpool dispatch");
    q_tpool_destroy(p);
    CHECK(g_cnt == 100, "tpool 100 任务全部执行");

    /* D3) 有界队列（上限 2，提交 5 应阻塞等待完成） */
    g_cnt = 0;
    q_tpool_t *p2 = q_tpool_create(2, 2);
    CHECK(p2 != NULL, "tpool 有界 create");
    for (int i = 0; i < 5; i++)
        CHECK(q_tpool_dispatch(p2, inc, NULL) == 0, "tpool 有界 dispatch");
    q_tpool_destroy(p2);
    CHECK(g_cnt == 5, "tpool 有界 5 任务全部执行");

    /* D4) 集成：reactor 事件 -> 派发到 worker 池 */
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
    q_tpool_destroy(wp); /* join worker：确保所有派发任务执行完毕后再断言 */
    CHECK(g_work >= 1, "reactor->worker 派发 >=1");
#endif /* __linux__ */

    if (g_fail == 0)
        printf("\nALL PASS\n");
    else
        printf("\n%d FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
