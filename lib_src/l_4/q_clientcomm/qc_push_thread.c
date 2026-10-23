/**
 * @file qc_push_thread.c
 * @brief M5：qc_push_thread —— 推送接收线程（阻塞收帧 -> demux -> 断线退避重建）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 设计要点：
 *   1) **无数据是常态**：服务端"有数据就推、没数据不推"，本线程绝大部分时间阻塞在
 *      recv。socket 带 30s 收发超时（qc_connect 设置），超时返回 EAGAIN 时
 *      **不是错误**，继续等即可 —— 与 keeper 靠心跳保活完全不同，这里没有心跳。
 *   2) **errno 必须每次调用前清零**：EOF（recv 返回 0）同样表现为 -1 且不设 errno，
 *      若沿用上一次超时残留的 EAGAIN，会把"对端已关闭"误判成"没数据"而死循环空转。
 *   3) 半开连接（对端宕机、无 FIN）靠 **TCP keepalive** 兜底：连接就绪后设置
 *      SO_KEEPALIVE + 激进探活参数，内核约 75s 内发现并杀掉，触发重建。
 *   4) 重建退避：base*2^(n-1) 上限 max，带 ±20% 抖动（与 keeper 同策略，防重连风暴）。
 *   5) 推送连接**不设 max_retry 上限**：实时流应尽可能自愈；真要放弃由 UI 显式
 *      调 qc_push_stop 决定（与 keeper 的 OFFLINE 语义刻意不同）。
 */
#include "qc_push_int.h"   /* 公开 API + 内部结构体 */
#include "qc_push_wire.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "include_ipc_proto.h"
#include "q_mem.h"

#ifdef Q_SYS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

/* keepalive 参数（秒）：空闲 45s 后每 10s 探一次，3 次无响应判死 -> 约 75s 发现半开 */
#define QC_PUSH_KA_IDLE  45
#define QC_PUSH_KA_INTVL 10
#define QC_PUSH_KA_CNT   3

/* 收帧缓冲上限：单条明文 <= netd 侧上限；轨迹/报警帧都是几十字节，给足余量 */
#define QC_PUSH_PLAIN_MAX 60000u

/* 判断本次 q_chan_receive 失败是否"只是没数据"（超时）而非连接故障 */
static int is_recv_timeout(void)
{
#ifdef Q_SYS_WINDOWS
    return (errno == WSAETIMEDOUT || errno == WSAEWOULDBLOCK);
#else
    return (errno == EAGAIN || errno == EWOULDBLOCK);
#endif
}

/* 设置 TCP keepalive（尽力而为：失败不影响主流程，只是半开发现变慢） */
static void set_keepalive(q_fd_t fd)
{
    if (fd == (q_fd_t)-1)
        return;
#ifdef Q_SYS_WINDOWS
    {
        struct tcp_keepalive ka;
        DWORD nbytes = 0;
        int   on = 1;
        if (setsockopt((SOCKET)fd, SOL_SOCKET, SO_KEEPALIVE,
                       (const char *)&on, sizeof(on)) != 0)
            return;
        ka.onoff = 1;
        ka.keepalivetime = QC_PUSH_KA_IDLE * 1000;
        ka.keepaliveinterval = QC_PUSH_KA_INTVL * 1000;
        (void)WSAIoctl((SOCKET)fd, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
                       NULL, 0, &nbytes, NULL, NULL);
    }
#else
    int on = 1;
    int idle = QC_PUSH_KA_IDLE, intvl = QC_PUSH_KA_INTVL, cnt = QC_PUSH_KA_CNT;
    if (setsockopt((int)fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) != 0)
        return;
#ifdef TCP_KEEPIDLE
    (void)setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    (void)setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#ifdef TCP_KEEPCNT
    (void)setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
#endif
}

/* 退避睡眠：分段睡，保证 stop 请求最多 100ms 内被响应 */
static void backoff_sleep(qc_push_t *p, uint32_t ms)
{
    uint64_t deadline = qc_now_ms() + ms;
    while (!p->stop)
    {
        uint64_t now = qc_now_ms();
        if (now >= deadline)
            break;
        {
            uint32_t chunk = (deadline - now < 100u) ? (uint32_t)(deadline - now)
                                                     : 100u;
            qc_sleep_ms(chunk);
        }
    }
}

/* 计算本次退避时长（ attempts 已自增；±20% 抖动防多客户端同步重连风暴） */
static uint32_t next_backoff_ms(qc_push_t *p)
{
    uint32_t bo = p->cfg.backoff_base_ms;
    uint32_t n = p->attempts;
    uint32_t jitter;

    while (n > 1u && bo < p->cfg.backoff_max_ms)
    {
        bo *= 2u;
        n--;
    }
    if (bo > p->cfg.backoff_max_ms)
        bo = p->cfg.backoff_max_ms;

    /* ±20%：用 q_rand 太重在锁外做不了，用线程本地轻量 LCG（仅影响重连节奏） */
    jitter = (uint32_t)(((uint64_t)bo * ((qc_now_ms() ^ (uint64_t)(uintptr_t)p) % 41u)) / 100u);
    if (jitter > bo / 5u)
        jitter = bo / 5u;
    return (qc_now_ms() & 1u) ? bo + jitter : bo - jitter;
}

QC_THREAD_RET qc_push_thread(QC_THREAD_ARG arg)
{
    qc_push_t *p = (qc_push_t *)arg;
    uint8_t   *plain;

    plain = (uint8_t *)malloc(QC_PUSH_PLAIN_MAX);
    if (!plain)
        return QC_THREAD_RET_VAL;

    set_keepalive(p->sess ? p->sess->fd : (q_fd_t)-1);

    for (;;)
    {
        /* ---------- 收帧循环：阻塞等服务端推数据 ---------- */
        while (!p->stop)
        {
            size_t   plen = 0;
            uint16_t cmd = 0;

            /* 每次收帧前清 errno：让 is_recv_timeout() 只反映本次调用 */
            errno = 0;
            if (q_chan_receive(p->sess->chan, p->sess->fd, plain,
                               QC_PUSH_PLAIN_MAX, &plen, &cmd) != 0)
            {
                if (p->stop)
                    break;
                if (is_recv_timeout())
                    continue;               /* 没数据：继续阻塞等 */
                break;                      /* 真故障：落到下面的重建段 */
            }

            switch (cmd)
            {
            case Q_CHAN_CMD_PUSH_TRAJ:
            {
                qc_traj_t t;
                if (qc_push_parse_traj(plain, plen, &t) == 0 && p->on_traj)
                    p->on_traj(&t, p->cb_ud);
                break;
            }
            case Q_CHAN_CMD_PUSH_ALARM:
            {
                qc_alarm_t a;
                if (qc_push_parse_alarm(plain, plen, &a) == 0 && p->on_alarm)
                    p->on_alarm(&a, p->cb_ud);
                break;
            }
            default:
                /* 推送连接上不应出现其它 cmd；忽略但不断连（防御性） */
                break;
            }
        }

        if (p->stop)
            break;

        /* ---------- 重建：退避 -> 重连 -> 成功则回到收帧循环 ---------- */
        {
            qc_mutex_lock(&p->lock);
            p->connected = 0;
            p->attempts++;
            qc_mutex_unlock(&p->lock);

            backoff_sleep(p, next_backoff_ms(p));
            if (p->stop)
                break;

            /* pwd_hash 经 on_relogin 向 UI 索取（内核不存口令） */
            if (qc_push_connect(p, NULL) == 0)
            {
                set_keepalive(p->sess->fd);
                continue;                   /* 回到收帧循环 */
            }
            /* 失败：继续退避重试（无次数上限，见文件头设计要点 5） */
        }
    }

    if (plain)
    {
        q_mem_zero(plain, QC_PUSH_PLAIN_MAX);
        free(plain);
    }
    return QC_THREAD_RET_VAL;
}
