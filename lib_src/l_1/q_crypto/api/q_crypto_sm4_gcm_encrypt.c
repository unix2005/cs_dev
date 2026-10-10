/**
 * @file q_crypto_sm4_gcm_encrypt.c
 * @brief q_crypto_sm4_gcm_encrypt —— SM4-GCM 加密（AEAD，基于 Tongsuo EVP_sm4_gcm）
 * @note  IV 必须由上层以 nonce_base XOR seq 派生（严禁随机），AAD 须含包头全部字段。
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_sm4_gcm_encrypt(const uint8_t *key, size_t keylen,
                             const uint8_t *iv, size_t ivlen,
                             const uint8_t *aad, size_t aadlen,
                             const uint8_t *pt, size_t ptlen,
                             uint8_t *ct, uint8_t tag[16], size_t *ctlen)
{
    if (!key || !iv || !ct || !tag || !ctlen)
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
        if (EVP_EncryptInit_ex(ctx, EVP_sm4_gcm(), NULL, NULL, NULL) != 1)
            break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)ivlen, NULL) != 1)
            break;
        if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1)
            break;
        if (aad && aadlen)
        {
            if (EVP_EncryptUpdate(ctx, NULL, &outl, aad, (int)aadlen) != 1)
                break;
        }
        if (pt && ptlen)
        {
            if (EVP_EncryptUpdate(ctx, ct, &outl, pt, (int)ptlen) != 1)
                break;
            *ctlen = (size_t)outl;
        }
        else
        {
            *ctlen = 0;
        }
        int finl = 0;
        if (EVP_EncryptFinal_ex(ctx, ct + *ctlen, &finl) != 1)
            break;
        *ctlen += (size_t)finl;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1)
            break;
        rc = 0;
    } while (0);

    EVP_CIPHER_CTX_free(ctx);
    return rc;
}
