/**
 * @file qc_push_sub_delta.c
 * @brief M5：qc_push_sub_delta —— 增量订阅（本地集合维护 + 分片发送）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 本地集合是**唯一真相**：
 *   - UI 每次勾选/取消只发变动 delta（省流量，也省服务端合并开销）；
 *   - 未连接时只改集合不发送 —— 连接恢复后由 qc_push_connect 全量重放，
 *     调用方因此完全不需要感知连接状态；
 *   - 大集合自动分片（Q_PUSH_SHARD_MAX/帧），UI 无需手动分批 ——
 *     一个单位 5000 辆车一次全订阅也就是 10 帧，服务端逐帧合并。
 */
#include "qc_push_int.h"   /* 公开 API + 内部结构体 */
#include "qc_push_wire.h"

#include <stdlib.h>
#include <string.h>

#include "q_mem.h"

/* 集合扩容：按 2 倍增长，初始 64。5000 辆车 = 80 次扩容内的 200KB，可接受 */
#define QC_PUSH_VIDS_INIT 64u

/* 帧缓冲：Q_PUSH_SHARD_MAX 个 ID 的最坏情况（4B TLV 头 + 40B 键）+ OP TLV + 余量 */
#define QC_PUSH_DELTA_BUF (Q_PUSH_SHARD_MAX * 48u + 64u)

/* 在集合中找 key；返回下标，找不到返回 -1（调用方须持锁） */
static int32_t sub_find_locked(const qc_push_t *p, const char *key)
{
    uint32_t i;
    for (i = 0; i < p->nvid; ++i)
    {
        if (strcmp(p->vids[i], key) == 0)
            return (int32_t)i;
    }
    return -1;
}

/* 集合插入（去重）；返回 0 已存在/新插入，-1 扩容失败（调用方须持锁） */
static int sub_add_locked(qc_push_t *p, const char *key)
{
    if (sub_find_locked(p, key) >= 0)
        return 0;                            /* 已在集合：幂等 */
    if (p->nvid >= p->cap_vid)
    {
        uint32_t ncap = p->cap_vid ? p->cap_vid * 2u : QC_PUSH_VIDS_INIT;
        char (*nv)[QC_PUSH_KEY_LEN];
        /* 上限保护：单客户端订阅不可能超过百万级，防止配置错误把内存吃光 */
        if (ncap > 1000000u)
            return -1;
        nv = (char (*)[QC_PUSH_KEY_LEN])realloc(p->vids,
                                                (size_t)ncap * QC_PUSH_KEY_LEN);
        if (!nv)
            return -1;
        p->vids = nv;
        p->cap_vid = ncap;
    }
    memset(p->vids[p->nvid], 0, QC_PUSH_KEY_LEN);
    strncpy(p->vids[p->nvid], key, QC_PUSH_KEY_LEN - 1u);
    p->nvid++;
    return 0;
}

/* 集合删除（交换尾部）；调用方须持锁 */
static void sub_remove_locked(qc_push_t *p, const char *key)
{
    int32_t idx = sub_find_locked(p, key);
    if (idx < 0)
        return;                              /* 不在集合：幂等 */
    if ((uint32_t)idx != p->nvid - 1u)
        memcpy(p->vids[idx], p->vids[p->nvid - 1u], QC_PUSH_KEY_LEN);
    p->nvid--;
}

/* 分片发送一次 delta（每帧 <= Q_PUSH_SHARD_MAX 个 ID）。
   fire-and-forget：SUB_DELTA 无需回执 —— TCP 保证有序到达，且本地集合是真相，
   丢帧的后果由"下次重连全量重放"兜底（订阅语义允许最终一致）。
   返回 0 全部发出；<0 发送失败（连接已坏，由接收线程负责重建）。 */
static int send_delta_shards(qc_push_t *p, int op,
                             const char *const *ids, uint32_t n)
{
    static const uint32_t shard = Q_PUSH_SHARD_MAX;
    uint32_t i;

    for (i = 0; i < n; i += shard)
    {
        uint8_t body[QC_PUSH_DELTA_BUF];
        size_t  blen = 0;
        uint32_t cnt = (n - i < shard) ? (n - i) : shard;

        if (qc_push_build_delta(body, sizeof(body), op, ids + i, cnt, &blen) != 0)
            return -1;
        if (q_chan_transmit(p->sess->chan, p->sess->fd, body, blen,
                            Q_CHAN_CMD_PUSH_SUB_DELTA) != 0)
            return -1;
        q_mem_zero(body, sizeof(body));      /* 虽然不含口令，习惯性清零 */
    }
    return 0;
}

int qc_push_sub_delta(qc_push_t *p, int op, const char *const *ids, uint32_t n)
{
    int send_rc = 0;
    uint32_t i;

    if (!p || !ids)
        return -1;
    if (op != Q_PUSH_OP_ADD && op != Q_PUSH_OP_REMOVE)
        return -1;

    /* 1) 先改本地集合（真相）—— 与连接状态无关 */
    qc_mutex_lock(&p->lock);
    for (i = 0; i < n; ++i)
    {
        if (!ids[i] || !ids[i][0])
            continue;
        if (strlen(ids[i]) >= QC_PUSH_KEY_LEN)
            continue;                        /* 超长键：本地也不收，防静默截断 */
        if (op == Q_PUSH_OP_ADD)
        {
            if (sub_add_locked(p, ids[i]) != 0)
            {
                qc_mutex_unlock(&p->lock);
                return -1;                   /* 内存不足：整体失败，UI 可重试 */
            }
        }
        else
        {
            sub_remove_locked(p, ids[i]);
        }
    }
    /* 2) 连接就绪才发送；未连接时集合已改，重连后自动重放 */
    if (p->connected && p->sess && p->sess->fd != (q_fd_t)-1)
        send_rc = send_delta_shards(p, op, ids, n);
    else
        send_rc = 0;
    if (send_rc != 0)
        p->connected = 0;                    /* 发送失败：连接已不可用 */
    qc_mutex_unlock(&p->lock);

    return send_rc;
}

uint32_t qc_push_sub_count(qc_push_t *p)
{
    uint32_t n;
    if (!p)
        return 0;
    qc_mutex_lock(&p->lock);
    n = p->nvid;
    qc_mutex_unlock(&p->lock);
    return n;
}

int qc_push_connected(qc_push_t *p)
{
    int c;
    if (!p)
        return 0;
    qc_mutex_lock(&p->lock);
    c = p->connected;
    qc_mutex_unlock(&p->lock);
    return c;
}
