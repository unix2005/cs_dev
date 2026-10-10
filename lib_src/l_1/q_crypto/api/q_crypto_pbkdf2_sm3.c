/**
 * @file q_crypto_pbkdf2_sm3.c
 * @brief q_crypto_pbkdf2_sm3 —— PBKDF2-HMAC-SM3（基于 Tongsuo PKCS5_PBKDF2_HMAC）
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_pbkdf2_sm3(const uint8_t *pass, size_t plen,
                        const uint8_t *salt, size_t slen,
                        uint32_t iter, uint8_t *out, size_t outlen)
{
    if (!pass || !salt || !out || iter == 0 || outlen == 0)
        return -1;

    if (PKCS5_PBKDF2_HMAC((const char *)pass, (int)plen,
                          salt, (int)slen, (int)iter,
                          EVP_sm3(), (int)outlen, out) != 1)
        return -1;
    return 0;
}
