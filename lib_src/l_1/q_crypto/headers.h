/**
 * @file headers.h
 * @brief q_crypto 模块统一聚合头文件（仅模块内 api/ 与 ut/ 源文件引用）
 * @note  集中引入公共基础头、Tongsuo（OpenSSL 兼容）头与对外头。
 *        规则要求：api/ 下每个 .c 只允许 `#include "headers.h"` 与 `#include "q_crypto.h"`。
 *        注意：本文件 include 了 Tongsuo 头，仅模块内可见；对外头 q_crypto.h 不暴露它们。
 *        另：api/q_crypto_sm2_internal.h 由本文件在 openssl 头之后引入（其内为静态内联，
 *        禁止再回头 include 本文件，避免环形包含）。
 */
#ifndef Q_CRYPTO_HEADERS_H
#define Q_CRYPTO_HEADERS_H

#include "include_stdio.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/err.h>
#include <openssl/x509.h> /* d2i_PUBKEY / i2d_PUBKEY */
#include <openssl/ec.h>   /* EC_KEY / EC_POINT（SM2 ECDH 派生用） */
#include <openssl/bn.h>   /* BIGNUM（SM2 ECDH 派生用） */

#include "q_crypto.h"
#include "api/q_crypto_sm2_internal.h"

#endif /* Q_CRYPTO_HEADERS_H */
