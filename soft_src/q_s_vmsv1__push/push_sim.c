/**
 * @file push_sim.c
 * @brief 推送微服务模拟线程：推进车辆 -> 按订阅/组织子树下发 TRAJ/ALARM
 *
 * 节奏（M5 裁定）：**无固定 2~5s 定时**，事件驱动"有数据即推"。
 * 桩数据没有真实 GPS 上行，故本线程就是数据源本身：每 tick 推进一次车辆位置，
 * 对【已订阅】且到点的车辆下发轨迹；报警按周期触发、按用户组织子树过滤。
 * 未订阅的车不发（订阅是精确语义，不是组织广播）。
 *
 * 线程模型：本线程是唯一的生产者/发送者（对所有会话），worker 只读 SUB_DELTA；
 * 全部发送持 g_lock 完成（见 srv_push.h 并发模型）。
 */
#include "srv_push.h"

#include <pthread.h>
#include <stdlib.h>

void *push_sim_thread(void *arg)
{
    (void)arg;

    push_stub_reset(q_get_ms_timestamp());

    while (!daemon_should_stop())
    {
        uint64_t now = q_get_ms_timestamp();

        pthread_mutex_lock(&g_lock);
        push_sim_deliver_locked(now);
        pthread_mutex_unlock(&g_lock);

        Q_SLEEP_MS(g_pcfg.tick_ms ? g_pcfg.tick_ms : PUSH_TICK_MS_DEF);
    }
    return NULL;
}

/* 供 main.c 使用：创建/Join 模拟线程（分离度由调用方决定，这里只包一层） */
int push_sim_start(pthread_t *tid)
{
    return pthread_create(tid, NULL, push_sim_thread, NULL) == 0 ? 0 : -1;
}

void push_sim_join(pthread_t tid)
{
    pthread_join(tid, NULL);
}
