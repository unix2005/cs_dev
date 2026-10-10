/**
 * @file headers.h
 * @brief q_crypto 模块统一聚合头文件（仅模块内 api/ 与 ut/ 源文件引用）
 * @note  集中引入公共基础头、Tongsuo（OpenSSL 兼容）头与对外头。
 *        规则要求：api/ut 下每个 .c 只允许 `#include "headers.h"` 与 `#include "q_crypto.h"`；
 *        所有系统头（stdio/string/stdint/errno 等）统一经 include_stdio.h 引入，
 *        禁止在 .c 直接 `#include` 系统头。
 *        注意：Tongsuo/OpenSSL 头经 include_tongsuo.h 聚合引入（第三方，非系统头），
 *        仅模块内可见；对外头 q_crypto.h 不暴露它们。
 *        另：api/q_crypto_sm2_internal.h 由本文件在 include_tongsuo.h（openssl 头）之后引入
 *        （其内为静态内联，禁止再回头 include 本文件，避免环形包含）。
 */
#ifndef Q_CRYPTO_HEADERS_H
#define Q_CRYPTO_HEADERS_H

#include "include_stdio.h"
#include "include_tongsuo.h"   /* 聚合 Tongsuo/OpenSSL 密码学头（EVP/HMAC/ERR/X509/EC/BN） */

#include "q_crypto.h"
#include "api/q_crypto_sm2_internal.h"

#endif /* Q_CRYPTO_HEADERS_H */
