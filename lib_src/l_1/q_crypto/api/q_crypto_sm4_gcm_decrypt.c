/**
 * @file q_crypto_sm4_gcm_decrypt.c
 * @brief q_crypto_sm4_gcm_decrypt —— SM4-GCM 解密并校验（AEAD，基于 Tongsuo EVP_sm4_gcm）
 * @note  tag 不匹配或任何错误均返回 -1（解密与认证绑定，GCM 标准行为）。
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_sm4_gcm_decrypt(const uint8_t *key, size_t keylen,
                             const uint8_t *iv, size_t ivlen,
                             const uint8_t *aad, size_t aadlen,
                             const uint8_t *ct, size_t ctlen,
                             const uint8_t tag[16],
                             uint8_t *pt, size_t *ptlen)
{
    if (!key || !iv || !ct || !tag || !pt || !ptlen)
        return -1;
    if (keylen != 16) /* SM4 密钥固定 16 字节 */
        return -1;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return -1;

    int rc = -1;
    do
    {
        int outl = 0;
        if (EVP_DecryptInit_ex(ctx, EVP_sm4_gcm(), NULL, NULL, NULL) != 1)
            break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)ivlen, NULL) != 1)
            break;
        if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1)
            break;
        if (aad && aadlen)
        {
            if (EVP_DecryptUpdate(ctx, NULL, &outl, aad, (int)aadlen) != 1)
                break;
        }
        if (ct && ctlen)
        {
            if (EVP_DecryptUpdate(ctx, pt, &outl, ct, (int)ctlen) != 1)
                break;
            *ptlen = (size_t)outl;
        }
        else
        {
            *ptlen = 0;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag) != 1)
            break;
        int finl = 0;
        /* DecryptFinal_ex 在校验失败时返回 0，解密与认证绑定 */
        if (EVP_DecryptFinal_ex(ctx, pt + *ptlen, &finl) != 1)
            break;
        *ptlen += (size_t)finl;
        rc = 0;
    } while (0);

    EVP_CIPHER_CTX_free(ctx);
    return rc;
}
