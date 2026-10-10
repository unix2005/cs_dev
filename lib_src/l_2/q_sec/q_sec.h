/**
 * @file q_sec.h
 * @brief l_2 能力层 安全能力库（libqsec）对外头文件
 * @layer  lib_src/l_2/q_sec  ->  产出 libqsec
 * @note   本文件自包含：仅依赖 include_stdio.h（提供 uint8_t/size_t 等）。
 *         职责（AI_SKILL_RULE §3.1 l_2）：
 *           - 密钥派生（SM3-KDF / 会话密钥）
 *           - 防重放（seq 滑动窗口，窗口 [last+1, last+64]，禁止"缓存 nonce"方案）
 *           - PoW（基于 SM3 的前导零位工作量证明）
 *           - 限流计数器（定长窗口）
 *         加解密原语一律来自 l_1/q_crypto（基于 Tongsuo），本库不做任何加解密算法。
 */
#ifndef L_2_Q_SEC_H
#define L_2_Q_SEC_H

#include "include_stdio.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* ===================== 密钥派生 ===================== */
    /* SM3-KDF（GB/T 32918.4）：以共享数据 z 派生 outlen 字节；成功 0，失败 -1 */
    int q_sec_kdf_derive(const uint8_t *z, size_t zlen, uint8_t *out, size_t outlen);

    /* 会话密钥派生：SM2 ECDH 共享密钥(32) 拼接 seq(8,大端) 后经 SM3-KDF 得 32 字节。
       成功 0，失败 -1；调用方负责清零 out 与临时共享缓冲（本函数内部已清零）。 */
    int q_sec_session_key(const uint8_t *my_priv_der, size_t my_priv_len,
                          const uint8_t *peer_pub_der, size_t peer_pub_len,
                          uint64_t seq, uint8_t out[32]);

    /* ===================== 防重放：seq 滑动窗口 ===================== */
    /* 窗口跨度 span（建议 64，内部裁剪到 [1,64]）。调用方负责 destroy。 */
    typedef struct q_sec_seq_window q_sec_seq_window_t;
    q_sec_seq_window_t *q_sec_seq_window_create(uint64_t span);
    void q_sec_seq_window_destroy(q_sec_seq_window_t *w);
    /* 接收 seq：在 (last, last+span] 且未重放 -> 接受返回 1；重放/越界返回 0；出错 -1。
       允许窗口内乱序补收（已收过的 seq 判为重放）。 */
    int q_sec_seq_window_accept(q_sec_seq_window_t *w, uint64_t seq);

    /* ===================== PoW：基于 SM3 的前导零位 ===================== */
    /* 求解：找 nonce 使 SM3(seed || nonce(8,大端)) 前 difficulty 位为 0；成功 0，失败 -1。
       difficulty 范围 [1,256]。 */
    int q_sec_pow_solve(const uint8_t *seed, size_t seedlen, int difficulty, uint64_t *nonce_out);
    /* 校验：满足难度返回 1，不满足 0，参数错 -1 */
    int q_sec_pow_verify(const uint8_t *seed, size_t seedlen, int difficulty, uint64_t nonce);

    /* ===================== 限流计数器（定长窗口） ===================== */
    /* 窗口 window_sec 内最多 max 次；window_sec=0 时不按时间重置（纯上限计数）。 */
    typedef struct q_sec_rate_limit q_sec_rate_limit_t;
    q_sec_rate_limit_t *q_sec_rate_limit_create(size_t max, uint64_t window_sec);
    void q_sec_rate_limit_destroy(q_sec_rate_limit_t *r);
    /* 记入一次事件(now_sec)；未超限返回 0，已超限返回 1，出错 -1 */
    int q_sec_rate_limit_hit(q_sec_rate_limit_t *r, uint64_t now_sec);

#ifdef __cplusplus
}
#endif

#endif /* L_2_Q_SEC_H */
