/**
 * @file q_crypto_sm3_kdf.c
 * @brief q_crypto_sm3_kdf —— SM3 密钥派生（GB/T 32918.4 计数模式 KDF）
 * @note  以 Tongsuo 的 SM3 为基础构造标准 KDF，本身为算法标准组合而非自研密码算法。
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_sm3_kdf(const uint8_t *z, size_t zlen, uint8_t *out, size_t outlen)
{
    if (!z || !out || outlen == 0)
        return -1;

    size_t blocks = (outlen + 31) / 32;
    for (size_t i = 0; i < blocks; i++)
    {
        uint32_t ct = (uint32_t)(i + 1);
        uint8_t ctb[4] = {
            (uint8_t)(ct >> 24), (uint8_t)(ct >> 16),
            (uint8_t)(ct >> 8), (uint8_t)ct}; /* 大端计数器 */

        uint8_t block[32];
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (!ctx)
            return -1;

        int ok = 1;
        ok &= (EVP_DigestInit_ex(ctx, EVP_sm3(), NULL) == 1);
        ok &= (EVP_DigestUpdate(ctx, z, zlen) == 1);
        ok &= (EVP_DigestUpdate(ctx, ctb, sizeof(ctb)) == 1);
        ok &= (EVP_DigestFinal_ex(ctx, block, NULL) == 1);
        EVP_MD_CTX_free(ctx);
        if (!ok)
            return -1;

        size_t take = (i == blocks - 1) ? (outlen - i * 32) : 32;
        memcpy(out + i * 32, block, take);
    }
    return 0;
}
