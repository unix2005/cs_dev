/**
 * @file q_crypto.h
 * @brief l_1 基础层 国密原语封装库（libqcrypto）对外头文件
 * @platform Windows/Linux/macOS
 * @layer  lib_src/l_1/q_crypto  ->  产出 libqcrypto
 * @note   本文件自包含：仅依赖公共基础头 include_stdio.h（提供 uint8_t/size_t），
 *         不直接暴露 Tongsuo/OpenSSL 类型，便于上层以 `#include <q_crypto.h>` 直接调用。
 *         api/ 下源文件仍只需 `#include "headers.h"` + `#include "q_crypto.h"`。
 *
 * 全部算法基于 Tongsuo（铜锁）提供的 SM2/SM3/SM4 实现，**禁止自研算法**
 * （AI_SKILL_RULE §3.1：国密原语封装）。覆盖：
 *   - SM3 哈希、SM3-KDF（GB/T 32918.4）、HMAC-SM3、PBKDF2-HMAC-SM3
 *   - SM4-GCM（AEAD）
 *   - SM2 密钥对生成、签名/验签、ECDH 密钥协商
 *   - HMAC-SM3 TOTP
 *
 * 安全约束（详见 AI_SKILL_RULE §3.2）：
 *   - SM4-GCM 的 IV 必须由上层用 nonce_base XOR seq 派生（严禁随机 IV）；
 *     包头全部字段必须整体作为 AAD 参与认证（由调用方负责组装 AAD）。
 *   - 密钥/临时敏感数据使用完毕应立即清零（调用方负责）。
 */
#ifndef L_1_Q_CRYPTO_H
#define L_1_Q_CRYPTO_H

#include "include_stdio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ===================== SM3 哈希 ===================== */
    /* 计算 SM3(data)，输出 32 字节；成功返回 0，失败 -1 */
    int q_crypto_sm3(const uint8_t *data, size_t dlen, uint8_t out[32]);

    /* ===================== SM3-KDF（GB/T 32918.4） ===================== */
    /* 以共享数据 z 派生 outlen 字节密钥材料；成功 0，失败 -1 */
    int q_crypto_sm3_kdf(const uint8_t *z, size_t zlen, uint8_t *out, size_t outlen);

    /* ===================== HMAC-SM3 ===================== */
    /* 输出 32 字节；成功 0，失败 -1 */
    int q_crypto_hmac_sm3(const uint8_t *key, size_t klen,
                          const uint8_t *data, size_t dlen, uint8_t out[32]);

    /* ===================== PBKDF2-HMAC-SM3 ===================== */
    /* 口令派生，iter 建议 >= 100000；成功 0，失败 -1 */
    int q_crypto_pbkdf2_sm3(const uint8_t *pass, size_t plen,
                            const uint8_t *salt, size_t slen,
                            uint32_t iter, uint8_t *out, size_t outlen);

    /* ===================== SM4-GCM（AEAD） ===================== */
    /* 加密：ct 缓冲区需 >= ptlen；ctlen 返回密文长度（= ptlen）；tag 输出 16 字节 */
    int q_crypto_sm4_gcm_encrypt(const uint8_t *key, size_t keylen,
                                 const uint8_t *iv, size_t ivlen,
                                 const uint8_t *aad, size_t aadlen,
                                 const uint8_t *pt, size_t ptlen,
                                 uint8_t *ct, uint8_t tag[16], size_t *ctlen);

    /* 解密并校验 tag：成功（tag 匹配）返回 0，tag 不匹配或出错返回 -1 */
    int q_crypto_sm4_gcm_decrypt(const uint8_t *key, size_t keylen,
                                 const uint8_t *iv, size_t ivlen,
                                 const uint8_t *aad, size_t aadlen,
                                 const uint8_t *ct, size_t ctlen,
                                 const uint8_t tag[16],
                                 uint8_t *pt, size_t *ptlen);

    /* ===================== SM2 ===================== */
    /* 生成 SM2 密钥对，导出 DER 编码（priv/pub）；调用方须 free() 两个缓冲区；成功 0，失败 -1 */
    int q_crypto_sm2_keygen(uint8_t **priv_der, size_t *priv_len,
                            uint8_t **pub_der, size_t *pub_len);

    /* 对 msg 签名，导出 DER 编码签名；调用方须 free() sig_der；成功 0，失败 -1 */
    int q_crypto_sm2_sign(const uint8_t *priv_der, size_t priv_len,
                          const uint8_t *msg, size_t mlen,
                          uint8_t **sig_der, size_t *sig_len);

    /* 验签：成功（有效）返回 1，无效返回 0，出错返回 -1 */
    int q_crypto_sm2_verify(const uint8_t *pub_der, size_t pub_len,
                            const uint8_t *msg, size_t mlen,
                            const uint8_t *sig_der, size_t sig_len);

    /* ECDH 密钥协商：由己方私钥 + 对端公钥派生 32 字节共享密钥；成功 0，失败 -1 */
    int q_crypto_sm2_derive(const uint8_t *my_priv_der, size_t my_priv_len,
                            const uint8_t *peer_pub_der, size_t peer_pub_len,
                            uint8_t out[32]);

    /* ===================== HMAC-SM3 TOTP ===================== */
    /* 基于 HMAC-SM3 的动态口令；digits 范围 [1,9]；成功 0，失败 -1 */
    int q_crypto_totp_sm3(const uint8_t *seed, size_t seedlen,
                          uint64_t counter, int digits, uint32_t *code);

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_CRYPTO_H */
