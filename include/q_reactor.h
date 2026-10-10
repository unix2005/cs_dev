/**
 * @file q_reactor.h
 * @brief l_1 基础层 通用「任务分发核心 + 可选事件源插件」模块（libqreactor）对外头文件
 * @layer  lib_src/l_1/q_reactor  ->  产出 libqreactor
 * @note   本模块是一个【与 fd / 网络连接 / IO 完全无关】的通用任务分配核心：
 *          1) q_disp_*  通用任务分发核心：拥有 N 个工作线程 + 任务队列 + 调度入口。
 *                      任意线程都可 q_disp_submit() 提交任务并行执行，或 q_disp_submit_local()
 *                      在事件循环线程内联执行“非 fd”回调（即 reactor 也能调度非 IO 任务）。
 *          2) q_evsrc_* 事件源插件接口：把“事件”抽象为可轮询的 fd + 就绪回调。
 *                       epoll 只是其中一种【可选】插件（仅 Linux），timerfd / 自定义源均可实现，
 *                       模块本身不再绑定到 epoll / socket。
 *          3) q_reactor_* 兼容旧 API（供 q_net 等沿用）：内部即 q_disp + epoll 插件，
 *                       语义与改造前一致（handler 在循环线程内联执行）。
 *          4) q_tpool_*  有界工作线程池（通用任务执行器，fd 无关）。
 *        整个核心不连接网络、不含任何 socket/accept/connect 逻辑。
 */
#ifndef L_1_Q_REACTOR_H
#define L_1_Q_REACTOR_H

#include "include_stdio.h"
#include "include_net.h"   /* epoll/timerfd 仅 Linux 可用；通用分发核心本身不依赖它 */

#ifdef __cplusplus
extern "C" {
#endif

    /* ===================== 通用回调类型 ===================== */
    typedef void (*q_task_fn)(void *arg);
    /* fd 事件回调（在循环线程内联执行，语义同改造前；仅 epoll 插件使用） */
    typedef void (*q_reactor_handler_t)(int fd, uint32_t events, void *ctx);

    /* ===================== 1. 通用任务分发核心（fd / IO 完全无关） ===================== */
    typedef struct q_disp q_disp_t;

    /* nworkers>=1 建立 N 个工作线程用于并行执行提交的任务；
       nworkers==0 则仅提供事件循环（无并行 worker，submit 返回 -1，submit_local 仍可用）。
       失败返回 NULL */
    q_disp_t *q_disp_create(int nworkers);
    void q_disp_destroy(q_disp_t *d);
    /* 提交任务到有界工作线程池并行执行（可跨线程调用）；无 worker 或参数错返回 -1，成功 0 */
    int  q_disp_submit(q_disp_t *d, q_task_fn fn, void *arg);
    /* 提交“本地任务”：在事件循环线程内联执行（用于让 reactor 调度非 fd 回调）；成功 0，失败 -1 */
    int  q_disp_submit_local(q_disp_t *d, q_task_fn fn, void *arg);
    /* 请求停止循环并唤醒阻塞中的 q_disp_run/loop */
    void q_disp_stop(q_disp_t *d);
    /* 唤醒阻塞中的循环（如跨线程提交本地任务后，使其尽快被调度） */
    int  q_disp_wake(q_disp_t *d);

    /* 事件源插件接口（epoll / timer / 自定义源均可实现，作为可选插件） */
    typedef struct q_evsrc q_evsrc_t;
    struct q_evsrc
    {
        int  (*fd)(q_evsrc_t *self);                  /* 可轮询的 fd；无则 -1 */
        void (*ready)(q_evsrc_t *self, q_disp_t *d);  /* fd 就绪：派发事件（内联于循环线程） */
        int  (*tick)(q_evsrc_t *self, q_disp_t *d, int timeout_ms); /* 无 fd 源的周期轮询(可选) */
        void (*destroy)(q_evsrc_t *self);
        void *priv;
    };
    /* 挂载一个事件源插件；返回 0 成功，-1 失败 */
    int  q_disp_add_src(q_disp_t *d, q_evsrc_t *src);
    /* 执行一轮调度（poll 所有源 fd + 分发就绪事件 + 内联本地任务）；0 成功，-1 出错 */
    int  q_disp_run(q_disp_t *d, int timeout_ms);
    /* 持续运行直到 *stop 或内部停止标志非 0；0 正常退出，-1 出错 */
    int  q_disp_loop(q_disp_t *d, volatile int *stop, int timeout_ms);

    /* ===================== 2. epoll 事件源插件（epoll 已降级为可选插件） ===================== */
    /* 接口无条件可见；真正实现仅 Linux（基于 epoll）。其它平台下这些函数返回 NULL / -1，
       通用分发核心 q_disp 不依赖它，因此模块在非 Linux 上仍可编译并运行“非 fd”任务派发。 */
    typedef struct q_evsrc_epoll q_evsrc_epoll_t;

    q_evsrc_epoll_t *q_evsrc_epoll_create(void);    /* Linux: 非阻塞 + close-on-exec；其它平台返回 NULL */
    void q_evsrc_epoll_destroy(q_evsrc_epoll_t *e);
    /* 注册 fd 与回调；events 为 EPOLLIN/EPOLLOUT 等；Linux 返回 0 成功，其它平台 -1 */
    int  q_evsrc_epoll_add(q_evsrc_epoll_t *e, int fd, uint32_t events,
                           q_reactor_handler_t h, void *ctx);
    int  q_evsrc_epoll_del(q_evsrc_epoll_t *e, int fd);
    /* 取插件接口，用于 q_disp_add_src 挂载 */
    q_evsrc_t *q_evsrc_epoll_as_src(q_evsrc_epoll_t *e);

    /* ===================== 3. 兼容旧 API（q_net 等沿用；内部基于 q_disp + epoll 插件） ===================== */
    typedef struct q_reactor q_reactor_t;
    q_reactor_t *q_reactor_create(void);
    void q_reactor_destroy(q_reactor_t *r);
    /* 注册 fd 与回调（经 epoll 插件）；events 为 EPOLLIN/EPOLLOUT 等；0 成功，-1 失败 */
    int q_reactor_add(q_reactor_t *r, int fd, uint32_t events,
                      q_reactor_handler_t h, void *ctx);
    int q_reactor_del(q_reactor_t *r, int fd);
    int q_reactor_run(q_reactor_t *r, int timeout_ms);
    int q_reactor_loop(q_reactor_t *r, volatile int *stop, int timeout_ms);

    /* ===================== 4. 有界工作线程池（通用任务执行器，fd 无关） ===================== */
    typedef struct q_tpool q_tpool_t;
    /* nthreads 工作线程数(>=1)；max_queue 任务队列上限(<=0 无界)；失败返回 NULL */
    q_tpool_t *q_tpool_create(int nthreads, int max_queue);
    /* 优雅关闭：等待队列任务执行完后退出 */
    void q_tpool_destroy(q_tpool_t *p);
    /* 提交任务；队列满或已关闭返回 -1，成功 0 */
    int q_tpool_dispatch(q_tpool_t *p, q_task_fn fn, void *arg);

    /* ===================== 5. 定时器辅助（仅 Linux timerfd；产生可被 epoll 源监听的 fd） ===================== */
#ifdef __linux__
    /* 创建：非阻塞 + close-on-exec；成功 0，失败 -1 */
    int q_reactor_timer_create(int *tfd_out);
    /* 首次 first_ms 后触发，之后每 interval_ms 触发（0 表示单次）；单位毫秒；成功 0，失败 -1 */
    int q_reactor_timer_arm(int tfd, uint64_t first_ms, uint64_t interval_ms);
#endif /* __linux__ */

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_REACTOR_H */
