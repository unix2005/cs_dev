/**
 * @file q_crypto_sm2_keygen.c
 * @brief q_crypto_sm2_keygen —— 生成 SM2 密钥对（基于 Tongsuo EVP_PKEY_SM2）
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_sm2_keygen(uint8_t **priv_der, size_t *priv_len,
                        uint8_t **pub_der, size_t *pub_len)
{
    if (!priv_der || !priv_len || !pub_der || !pub_len)
        return -1;

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_SM2, NULL);
    if (!ctx)
        return -1;

    if (EVP_PKEY_keygen_init(ctx) != 1)
    {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }

    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(ctx, &pkey) != 1)
    {
        EVP_PKEY_CTX_free(ctx);
        return -1;
    }
    EVP_PKEY_CTX_free(ctx);

    if (q_crypto_sm2_export(pkey, priv_der, priv_len, 1) != 0)
    {
        EVP_PKEY_free(pkey);
        return -1;
    }
    if (q_crypto_sm2_export(pkey, pub_der, pub_len, 0) != 0)
    {
        free(*priv_der);
        EVP_PKEY_free(pkey);
        return -1;
    }
    EVP_PKEY_free(pkey);
    return 0;
}
