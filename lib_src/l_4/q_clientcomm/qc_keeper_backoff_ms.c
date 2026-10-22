/**
 * @file qc_keeper_backoff_ms.c
 * @brief M4 会话治理：断线重连退避时长计算
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 序列（默认 base=1s, max=30s）：1s -> 2s -> 4s -> 8s -> 16s -> 30s（封顶）。
 *
 * 为什么必须带抖动：护网场景下服务端重启 / 网络抖动会让**所有客户端同时**发现断线，
 * 若都按固定 1/2/4s 重试，会形成重连风暴（thundering herd），
 * 把刚起来的服务再次打垮 —— 抖动把重试时刻打散，是这类场景的标准做法。
 */
#include "qc_keeper.h"

uint32_t qc_keeper_backoff_ms(const struct qc_session *s, uint32_t attempts)
{
    uint32_t base = s->cfg.backoff_base_sec ? s->cfg.backoff_base_sec : 1u;
    uint32_t max  = s->cfg.backoff_max_sec  ? s->cfg.backoff_max_sec  : 30u;
    uint64_t d    = (uint64_t)base * 1000u;
    uint32_t i;
    uint32_t jitter;

    if (attempts == 0u)
        attempts = 1u;

    for (i = 1u; i < attempts && i < 16u; ++i)
        d *= 2u;                       /* base * 2^(attempts-1) */

    if (d > (uint64_t)max * 1000u)
        d = (uint64_t)max * 1000u;

    /* ±20% 抖动：用 (时刻, 次数) 派生的伪随机，避免引入额外依赖与全局状态 */
    jitter = (uint32_t)((qc_now_ms() ^ ((uint64_t)attempts * 2654435761u)) % 41u);
    d = d * (100u - 20u + jitter) / 100u;

    return (uint32_t)d;
}
