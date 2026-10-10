/**
 * @file q_reactor.h
 * @brief l_1 基础层 通用「Reactor + Worker 线程池」模块（libqreactor）对外头文件
 * @layer  lib_src/l_1/q_reactor  ->  产出 libqreactor
 * @note   职责：基于 epoll 的通用事件反应器（对 fd 通用，不限于 socket）+ 有界工作线程池
 *         + 通用定时器（timerfd）。**不连接网络、不含任何 socket/accept/connect 逻辑**，
 *         供上层（如 q_net 的网络事件、或其它任意 fd 的 IO）复用同一套 Reactor+Worker 模式。
 */
#ifndef L_1_Q_REACTOR_H
#define L_1_Q_REACTOR_H

#include "include_stdio.h"
#include "include_net.h"   /* 提供 epoll 接口与 EPOLLIN/EPOLLOUT 等事件宏（目标平台 Linux） */

#ifdef __cplusplus
extern "C" {
#endif

    /* ===================== 通用事件反应器（epoll，对 fd 通用） ===================== */
    typedef struct q_reactor q_reactor_t;
    typedef void (*q_reactor_handler_t)(int fd, uint32_t events, void *ctx);

    q_reactor_t *q_reactor_create(void);
    void q_reactor_destroy(q_reactor_t *r);
    /* 注册 fd 与回调；events 为 EPOLLIN/EPOLLOUT 等；返回 0 成功，-1 失败 */
    int q_reactor_add(q_reactor_t *r, int fd, uint32_t events,
                      q_reactor_handler_t h, void *ctx);
    int q_reactor_del(q_reactor_t *r, int fd);
    /* 执行一轮事件循环（单次 epoll_wait + 分发）；0 成功，-1 出错 */
    int q_reactor_run(q_reactor_t *r, int timeout_ms);
    /* 持续运行直到 *stop 非 0（每次循环用 timeout_ms 轮询 stop 标志）；0 正常退出，-1 出错 */
    int q_reactor_loop(q_reactor_t *r, volatile int *stop, int timeout_ms);

    /* ===================== 有界工作线程池 ===================== */
    typedef struct q_tpool q_tpool_t;
    typedef void (*q_task_fn)(void *arg);
    /* nthreads 工作线程数(>=1)；max_queue 任务队列上限(<=0 无界)；失败返回 NULL */
    q_tpool_t *q_tpool_create(int nthreads, int max_queue);
    /* 优雅关闭：等待队列任务执行完后退出 */
    void q_tpool_destroy(q_tpool_t *p);
    /* 提交任务；队列满或已关闭返回 -1，成功 0 */
    int q_tpool_dispatch(q_tpool_t *p, q_task_fn fn, void *arg);

    /* ===================== 通用定时器（timerfd，CLOCK_MONOTONIC） ===================== */
    /* 创建：非阻塞 + close-on-exec；成功 0，失败 -1 */
    int q_reactor_timer_create(int *tfd_out);
    /* 首次 first_ms 后触发，之后每 interval_ms 触发（0 表示单次）；单位毫秒；成功 0，失败 -1 */
    int q_reactor_timer_arm(int tfd, uint64_t first_ms, uint64_t interval_ms);

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_REACTOR_H */
