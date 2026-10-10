/**
 * @file q_sec_kdf_derive.c
 * @brief q_sec_kdf_derive —— SM3-KDF 派生（封装 q_crypto_sm3_kdf）
 */
#include "headers.h"
#include "q_sec.h"
#include "q_crypto.h"

int q_sec_kdf_derive(const uint8_t *z, size_t zlen, uint8_t *out, size_t outlen)
{
    if (!z || zlen == 0 || !out || outlen == 0)
        return -1;
    return q_crypto_sm3_kdf(z, zlen, out, outlen);
}
