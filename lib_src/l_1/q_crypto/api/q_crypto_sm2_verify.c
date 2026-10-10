/**
 * @file q_crypto_sm2_verify.c
 * @brief q_crypto_sm2_verify —— SM2 验签（基于 Tongsuo EVP_DigestVerify + EVP_sm3）
 * @note  返回 1=有效，0=无效，-1=出错（与签名共用默认用户标识，保持一致性）。
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_sm2_verify(const uint8_t *pub_der, size_t pub_len,
                        const uint8_t *msg, size_t mlen,
                        const uint8_t *sig_der, size_t sig_len)
{
    if (!pub_der || !msg || !sig_der)
        return -1;

    EVP_PKEY *pkey = q_crypto_sm2_load_pub(pub_der, pub_len);
    if (!pkey)
        return -1;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        EVP_PKEY_free(pkey);
        return -1;
    }

    int rc = -1;
    /* SM2 必须以摘要名 "SM3" 经 _ex 初始化，匹配 SM2 签名方案（直接传 EVP_sm3() 在 Tongsuo 下崩溃） */
    if (EVP_DigestVerifyInit_ex(ctx, NULL, "SM3", NULL, NULL, pkey, NULL) == 1)
        rc = EVP_DigestVerify(ctx, sig_der, sig_len, msg, mlen);

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}
