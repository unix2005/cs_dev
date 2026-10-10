/**
 * @file q_crypto_totp_sm3.c
 * @brief q_crypto_totp_sm3 —— 基于 HMAC-SM3 的动态口令（TOTP）
 * @note  以 counter（如 time/步长）大端作为 HMAC 输入，动态截断取 digits 位。
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_totp_sm3(const uint8_t *seed, size_t seedlen,
                      uint64_t counter, int digits, uint32_t *code)
{
    if (!seed || !code || digits < 1 || digits > 9)
        return -1;

    uint8_t ctr[8];
    for (int i = 0; i < 8; i++)
        ctr[i] = (uint8_t)(counter >> (56 - 8 * i));

    uint8_t hmac[32];
    if (q_crypto_hmac_sm3(seed, seedlen, ctr, sizeof(ctr), hmac) != 0)
        return -1;

    int off = hmac[31] & 0x0F;
    uint32_t bin = ((uint32_t)(hmac[off] & 0x7F) << 24) |
                   ((uint32_t)hmac[off + 1] << 16) |
                   ((uint32_t)hmac[off + 2] << 8) |
                   (uint32_t)hmac[off + 3];

    uint32_t mod = 1;
    for (int i = 0; i < digits; i++)
        mod *= 10;

    *code = bin % mod;
    return 0;
}
