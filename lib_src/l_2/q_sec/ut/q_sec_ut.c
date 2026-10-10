/**
 * @file q_sec_ut.c
 * @brief q_sec 单元测试：KDF / 会话密钥 / 滑动窗口 / PoW / 限流
 */
#include "headers.h"
#include "q_sec.h"
#include "q_crypto.h"
#include "q_mem.h"

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); g_fail++; } \
                      else printf("PASS: %s\n", m); } while (0)

int main(void)
{
    /* KDF：与底层 q_crypto_sm3_kdf 行为一致（长度与确定性） */
    uint8_t z[16]; for (int i = 0; i < 16; i++) z[i] = (uint8_t)i;
    uint8_t o1[32], o2[32];
    CHECK(q_sec_kdf_derive(z, sizeof(z), o1, 32) == 0, "kdf derive");
    CHECK(q_sec_kdf_derive(z, sizeof(z), o2, 32) == 0, "kdf derive again");
    CHECK(memcmp(o1, o2, 32) == 0, "kdf 确定性");
    CHECK(q_sec_kdf_derive(NULL, 0, o1, 32) == -1, "kdf 参数错 -> -1");

    /* 会话密钥：双方各自用对方公钥派生应相等 */
    uint8_t *ap = NULL, *app = NULL, *bp = NULL, *bpp = NULL;
    size_t apl = 0, appl = 0, bpl = 0, bppl = 0;
    CHECK(q_crypto_sm2_keygen(&ap, &apl, &app, &appl) == 0, "sm2 keygen A");
    CHECK(q_crypto_sm2_keygen(&bp, &bpl, &bpp, &bppl) == 0, "sm2 keygen B");
    uint8_t ka[32], kb[32];
    CHECK(q_sec_session_key(ap, apl, bpp, bppl, 0x1234ull, ka) == 0, "session_key A->B");
    CHECK(q_sec_session_key(bp, bpl, app, appl, 0x1234ull, kb) == 0, "session_key B->A");
    CHECK(memcmp(ka, kb, 32) == 0, "会话密钥双向一致");
    CHECK(q_sec_session_key(ap, apl, bpp, bppl, 0x9999ull, ka) == 0, "session_key 不同 seq");
    CHECK(memcmp(ka, kb, 32) != 0, "不同 seq 派生不同密钥");
    q_mem_zero(ka, 32); q_mem_zero(kb, 32);
    free(ap); free(app); free(bp); free(bpp);

    /* seq 滑动窗口 */
    q_sec_seq_window_t *w = q_sec_seq_window_create(64);
    CHECK(w != NULL, "seq_window create");
    CHECK(q_sec_seq_window_accept(w, 100) == 1, "seq 首个接受");
    CHECK(q_sec_seq_window_accept(w, 100) == 0, "seq 重放拒绝");
    CHECK(q_sec_seq_window_accept(w, 101) == 1, "seq 递增接受");
    CHECK(q_sec_seq_window_accept(w, 105) == 1, "seq 跳进窗口接受");
    CHECK(q_sec_seq_window_accept(w, 105) == 0, "seq 已收重放拒绝");
    CHECK(q_sec_seq_window_accept(w, 90) == 0, "seq 回退过期拒绝");
    CHECK(q_sec_seq_window_accept(w, 1000) == 0, "seq 超前超窗拒绝");
    /* 仅前向窗口：已越过的高水位之后不可再接收（窗口内不乱序补收） */
    CHECK(q_sec_seq_window_accept(w, 102) == 0, "seq 落后窗口拒绝（无乱序补收）");
    CHECK(q_sec_seq_window_accept(w, 102) == 0, "seq 再次拒绝");
    q_sec_seq_window_destroy(w);

    /* PoW */
    uint8_t seed[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint64_t nonce = 0;
    CHECK(q_sec_pow_solve(seed, sizeof(seed), 8, &nonce) == 0, "pow solve (8 bits)");
    CHECK(q_sec_pow_verify(seed, sizeof(seed), 8, nonce) == 1, "pow verify ok");
    CHECK(q_sec_pow_verify(seed, sizeof(seed), 8, nonce + 1) == 0, "pow verify 错误 nonce -> 0");
    CHECK(q_sec_pow_solve(seed, sizeof(seed), 1, &nonce) == 0, "pow solve (1 bit)");
    CHECK(q_sec_pow_verify(seed, sizeof(seed), 1, nonce) == 1, "pow verify 1 bit");

    /* 限流：窗口 10s 内最多 3 次 */
    q_sec_rate_limit_t *r = q_sec_rate_limit_create(3, 10);
    CHECK(r != NULL, "rate_limit create");
    CHECK(q_sec_rate_limit_hit(r, 1000) == 0, "rate hit 1");
    CHECK(q_sec_rate_limit_hit(r, 1001) == 0, "rate hit 2");
    CHECK(q_sec_rate_limit_hit(r, 1002) == 0, "rate hit 3");
    CHECK(q_sec_rate_limit_hit(r, 1003) == 1, "rate 超限 -> 1");
    CHECK(q_sec_rate_limit_hit(r, 1011) == 0, "rate 新窗口重置");
    q_sec_rate_limit_destroy(r);

    if (g_fail == 0) printf("\nALL PASS\n");
    else printf("\n%d FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
