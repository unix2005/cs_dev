/**
 * @file netd_limit.c
 * @brief netd 限流矩阵（技术方案 V2 §6.4）：IP 维度并发 / 新建速率 / 登录失败拉黑
 * @note 全局配额（g_netd.conn_total）与 IP 表在本文件内由 g_netd.lock 保护。
 *       源 IP 键为 netd_addr_t（v4/v6 统一）；wire 侧 token/src_ip 仍为 uint32 IPv4。
 */
#include "srv_netd.h"

static netd_ipent_t g_iptab[NETD_IPTAB];

/* netd_addr_t 的 FNV-1a 哈希（覆盖 fam + 16 字节地址） */
static uint32_t addr_hash(const netd_addr_t *a)
{
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)a;
    size_t i;
    for (i = 0u; i < sizeof(*a); i++)
    {
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h % NETD_IPTAB;
}

/* 取（或惰性创建）IP 表项；表满返回 NULL */
static netd_ipent_t *ipent_get(const netd_addr_t *src)
{
    uint32_t h = addr_hash(src);
    uint32_t i;

    for (i = 0u; i < NETD_IPTAB; i++)
    {
        netd_ipent_t *e = &g_iptab[(h + i) % NETD_IPTAB];
        if (e->addr.fam == src->fam && memcmp(e->addr.b, src->b, 16) == 0)
            return e;
        if (e->addr.fam == 0u)                       /* 空槽：占用并初始化 */
        {
            e->addr      = *src;
            e->conns     = 0;
            e->blk_until = 0;
            e->rl_new    = q_sec_rate_limit_create(NETD_PERIP_NEW, 1u);
            e->rl_fail   = q_sec_rate_limit_create(NETD_PERIP_FAIL, (uint64_t)NETD_PERIP_FAIL_WIN);
            return e;
        }
    }
    return NULL;
}

/* 准入：返回 0 表示允许并已占用配额；非 0 表示拒绝 */
int conn_acquire(netd_ctx_t *g, const netd_addr_t *src)
{
    int rc = -1;
    netd_ipent_t *e;

    pthread_mutex_lock(&g->lock);
    if (g->conn_total >= NETD_GLOBAL_CONN)
        goto out;

    e = ipent_get(src);
    if (!e)
        goto out;

    if (e->blk_until && (time_t)now_sec() < e->blk_until)
        goto out;
    if (e->conns >= NETD_PERIP_CONN)
        goto out;
    if (q_sec_rate_limit_hit(e->rl_new, now_sec()) != 0)
        goto out;

    e->conns++;
    g->conn_total++;
    rc = 0;
out:
    pthread_mutex_unlock(&g->lock);
    return rc;
}

void conn_release(netd_ctx_t *g, const netd_addr_t *src)
{
    netd_ipent_t *e;

    pthread_mutex_lock(&g->lock);
    e = ipent_get(src);
    if (e && e->conns > 0)
        e->conns--;
    if (g->conn_total > 0)
        g->conn_total--;
    pthread_mutex_unlock(&g->lock);
}

/* 登录失败计数：达到阈值则拉黑该 IP */
void fail_note(netd_ctx_t *g, const netd_addr_t *src)
{
    netd_ipent_t *e;

    pthread_mutex_lock(&g->lock);
    e = ipent_get(src);
    if (e && q_sec_rate_limit_hit(e->rl_fail, now_sec()) == 1)
    {
        e->blk_until = (time_t)now_sec() + (time_t)NETD_PERIP_BLK_SEC;
        QLOG_WARN(g->log, "登录失败超阈值，IP 拉黑 %u 秒", (unsigned)NETD_PERIP_BLK_SEC);
    }
    pthread_mutex_unlock(&g->lock);
}
