/**
 * @file q_crypto_ut.c
 * @brief q_crypto 单元测试小程序（构建后由 make ut 运行）
 */
#include "headers.h"
#include "q_crypto.h"

#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                              \
    do                                                \
    {                                                 \
        if (!(cond))                                  \
        {                                             \
            printf("FAIL: %s\n", msg);                \
            g_fail++;                                 \
        }                                             \
        else                                          \
        {                                             \
            printf("PASS: %s\n", msg);               \
        }                                             \
    } while (0)

static int hex2bin(const char *h, uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        int hi, lo;
        char c1 = h[2 * i], c2 = h[2 * i + 1];
        if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
        else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
        else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
        else return -1;
        if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
        else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
        else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
        else return -1;
        b[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

int main(void)
{
    /* ============ SM3 已知应答（GM/T 0004-2012） ============ */
    uint8_t sm3_out[32];
    CHECK(q_crypto_sm3((const uint8_t *)"", 0, sm3_out) == 0, "sm3(empty)");
    uint8_t expect[32];
    hex2bin("1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b", expect, 32);
    CHECK(memcmp(sm3_out, expect, 32) == 0, "sm3(empty) known-answer");

    CHECK(q_crypto_sm3((const uint8_t *)"abc", 3, sm3_out) == 0, "sm3(abc)");
    hex2bin("66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0", expect, 32);
    CHECK(memcmp(sm3_out, expect, 32) == 0, "sm3(abc) known-answer");

    /* ============ SM3-KDF ============ */
    uint8_t z[16];
    memset(z, 0x11, sizeof(z));
    uint8_t k1[32], k2[32], k3[32];
    CHECK(q_crypto_sm3_kdf(z, sizeof(z), k1, 32) == 0, "kdf len32");
    CHECK(q_crypto_sm3_kdf(z, sizeof(z), k2, 32) == 0, "kdf len32 #2");
    CHECK(memcmp(k1, k2, 32) == 0, "kdf deterministic");
    uint8_t z2[16];
    memset(z2, 0x22, sizeof(z2));
    CHECK(q_crypto_sm3_kdf(z2, sizeof(z2), k3, 32) == 0, "kdf len32 z2");
    CHECK(memcmp(k1, k3, 32) != 0, "kdf differs by z");
    uint8_t k4[40];
    CHECK(q_crypto_sm3_kdf(z, sizeof(z), k4, 40) == 0, "kdf non-32-multiple len");

    /* ============ HMAC-SM3 ============ */
    uint8_t hk[32], hk2[32], hk3[32];
    CHECK(q_crypto_hmac_sm3((const uint8_t *)"key", 3, (const uint8_t *)"msg", 3, hk) == 0, "hmac ok");
    q_crypto_hmac_sm3((const uint8_t *)"key", 3, (const uint8_t *)"msg", 3, hk2);
    CHECK(memcmp(hk, hk2, 32) == 0, "hmac deterministic");
    q_crypto_hmac_sm3((const uint8_t *)"KEY", 3, (const uint8_t *)"msg", 3, hk3);
    CHECK(memcmp(hk, hk3, 32) != 0, "hmac key-sensitive");

    /* ============ PBKDF2-HMAC-SM3 ============ */
    uint8_t dk1[32], dk2[32], dk3[32];
    CHECK(q_crypto_pbkdf2_sm3((const uint8_t *)"pw", 2, (const uint8_t *)"salt", 4, 1000, dk1, 32) == 0, "pbkdf2 iter1000");
    q_crypto_pbkdf2_sm3((const uint8_t *)"pw", 2, (const uint8_t *)"salt", 4, 1000, dk2, 32);
    CHECK(memcmp(dk1, dk2, 32) == 0, "pbkdf2 deterministic");
    q_crypto_pbkdf2_sm3((const uint8_t *)"pw", 2, (const uint8_t *)"salt", 4, 1001, dk3, 32);
    CHECK(memcmp(dk1, dk3, 32) != 0, "pbkdf2 iter-sensitive");

    /* ============ SM4-GCM ============ */
    uint8_t key[16];
    memset(key, 0x01, sizeof(key));
    uint8_t iv[12];
    memset(iv, 0x02, sizeof(iv));
    uint8_t aad[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const char *pt = "secret message";
    size_t ptlen = strlen(pt);
    uint8_t ct[64], tag[16];
    size_t ctlen = 0;
    CHECK(q_crypto_sm4_gcm_encrypt(key, 16, iv, 12, aad, 8, (const uint8_t *)pt, ptlen, ct, tag, &ctlen) == 0, "gcm encrypt");
    CHECK(ctlen == ptlen, "gcm ctlen == ptlen");
    uint8_t dec[64];
    size_t declen = 0;
    CHECK(q_crypto_sm4_gcm_decrypt(key, 16, iv, 12, aad, 8, ct, ctlen, tag, dec, &declen) == 0, "gcm decrypt");
    CHECK(declen == ptlen && memcmp(dec, pt, ptlen) == 0, "gcm roundtrip");
    uint8_t ct2[64];
    memcpy(ct2, ct, ctlen);
    ct2[0] ^= 0xff;
    uint8_t dec2[64];
    size_t dl2 = 0;
    CHECK(q_crypto_sm4_gcm_decrypt(key, 16, iv, 12, aad, 8, ct2, ctlen, tag, dec2, &dl2) == -1, "gcm tampered-ct rejected");
    uint8_t aad2[8];
    memcpy(aad2, aad, 8);
    aad2[0] ^= 0xff;
    uint8_t dec3[64];
    size_t dl3 = 0;
    CHECK(q_crypto_sm4_gcm_decrypt(key, 16, iv, 12, aad2, 8, ct, ctlen, tag, dec3, &dl3) == -1, "gcm tampered-aad rejected");

    /* ============ SM2 ============ */
    uint8_t *ap = NULL, *apu = NULL, *bp = NULL, *bpu = NULL;
    size_t apl = 0, apul = 0, bpl = 0, bpul = 0;
    CHECK(q_crypto_sm2_keygen(&ap, &apl, &apu, &apul) == 0, "sm2 keygen A");
    CHECK(q_crypto_sm2_keygen(&bp, &bpl, &bpu, &bpul) == 0, "sm2 keygen B");

    const char *m = "hello sm2";
    uint8_t *sig = NULL;
    size_t sl = 0;
    CHECK(q_crypto_sm2_sign(ap, apl, (const uint8_t *)m, strlen(m), &sig, &sl) == 0, "sm2 sign");
    CHECK(q_crypto_sm2_verify(apu, apul, (const uint8_t *)m, strlen(m), sig, sl) == 1, "sm2 verify good");
    CHECK(q_crypto_sm2_verify(apu, apul, (const uint8_t *)"tampered", 8, sig, sl) == 0, "sm2 verify bad msg -> 0");

    uint8_t s1[32], s2[32];
    CHECK(q_crypto_sm2_derive(ap, apl, bpu, bpul, s1) == 0, "sm2 derive A");
    CHECK(q_crypto_sm2_derive(bp, bpl, apu, apul, s2) == 0, "sm2 derive B");
    CHECK(memcmp(s1, s2, 32) == 0, "sm2 derive shared equal");

    free(ap);
    free(apu);
    free(bp);
    free(bpu);
    free(sig);

    /* ============ HMAC-SM3 TOTP ============ */
    uint8_t seed[16];
    memset(seed, 0xab, sizeof(seed));
    uint32_t c1 = 0, c2 = 0;
    CHECK(q_crypto_totp_sm3(seed, sizeof(seed), 12345, 6, &c1) == 0, "totp gen");
    q_crypto_totp_sm3(seed, sizeof(seed), 12345, 6, &c2);
    CHECK(c1 == c2, "totp deterministic");
    CHECK(c1 < 1000000, "totp 6-digit bound");
    uint32_t c3 = 0;
    q_crypto_totp_sm3(seed, sizeof(seed), 12346, 6, &c3);
    (void)c3; /* 不同 counter 通常不同（弱校验，仅确保不崩溃） */

    if (g_fail == 0)
        printf("\nALL PASS\n");
    else
        printf("\n%d FAILED\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
