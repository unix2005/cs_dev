/**
 * @file q_crypto_sm2_sign.c
 * @brief q_crypto_sm2_sign —— SM2 签名（基于 Tongsuo EVP_DigestSign + EVP_sm3）
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_sm2_sign(const uint8_t *priv_der, size_t priv_len,
                      const uint8_t *msg, size_t mlen,
                      uint8_t **sig_der, size_t *sig_len)
{
    if (!priv_der || !msg || !sig_der || !sig_len)
        return -1;

    EVP_PKEY *pkey = q_crypto_sm2_load_priv(priv_der, priv_len);
    if (!pkey)
        return -1;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        EVP_PKEY_free(pkey);
        return -1;
    }

    int rc = -1;
    uint8_t *sig = NULL;
    size_t sigsz = 0;
    do
    {
        /* SM2 必须以摘要名 "SM3" 经 _ex 初始化，匹配 SM2 签名方案（直接传 EVP_sm3() 在 Tongsuo 下崩溃） */
        if (EVP_DigestSignInit_ex(ctx, NULL, "SM3", NULL, NULL, pkey, NULL) != 1)
            break;
        if (EVP_DigestSign(ctx, NULL, &sigsz, msg, mlen) != 1)
            break;
        sig = (uint8_t *)malloc(sigsz);
        if (!sig)
            break;
        if (EVP_DigestSign(ctx, sig, &sigsz, msg, mlen) != 1)
            break;
        *sig_der = sig;
        *sig_len = sigsz;
        rc = 0;
    } while (0);

    if (rc != 0)
        free(sig);

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}
