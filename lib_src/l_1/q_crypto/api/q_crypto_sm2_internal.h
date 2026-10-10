/**
 * @file q_crypto_sm2_internal.h
 * @brief q_crypto 内部 SM2 DER 密钥导入/导出辅助（静态内联，由各 sm2_*.c 复用）
 * @note  本文件被 headers.h 在 <openssl/evp.h> 之后引入，故 EVP_PKEY 等类型已可用；
 *        禁止再 include headers.h（否则环形包含）；仅声明静态内联，不生成独立符号。
 */
#ifndef Q_CRYPTO_SM2_INTERNAL_H
#define Q_CRYPTO_SM2_INTERNAL_H

#include <stdint.h>
/* 注：本文件被 headers.h 在 <openssl/evp.h>/<openssl/x509.h> 之后引入，
   EVP_PKEY / d2i_PUBKEY / i2d_PUBKEY 等类型已可用，故此处仅保留 stdint.h。 */

static EVP_PKEY *q_crypto_sm2_load_priv(const uint8_t *der, size_t len)
{
    if (!der || len == 0)
        return NULL;
    const uint8_t *p = der;
    return d2i_PrivateKey(EVP_PKEY_SM2, NULL, &p, (long)len);
}

static EVP_PKEY *q_crypto_sm2_load_pub(const uint8_t *der, size_t len)
{
    if (!der || len == 0)
        return NULL;
    const uint8_t *p = der;
    return d2i_PUBKEY(NULL, &p, (long)len);
}

/* 导出为 DER，使用 malloc 分配（调用方 free()），避免与 OPENSSL_free 混用 */
static int q_crypto_sm2_export(EVP_PKEY *pkey, uint8_t **out, size_t *outlen, int is_priv)
{
    if (!pkey || !out || !outlen)
        return -1;

    int len = is_priv ? i2d_PrivateKey(pkey, NULL) : i2d_PUBKEY(pkey, NULL);
    if (len <= 0)
        return -1;

    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf)
        return -1;

    uint8_t *p = buf;
    if (is_priv)
        i2d_PrivateKey(pkey, &p);
    else
        i2d_PUBKEY(pkey, &p);

    *out = buf;
    *outlen = (size_t)len;
    return 0;
}

#endif /* Q_CRYPTO_SM2_INTERNAL_H */
