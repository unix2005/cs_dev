/**
 * @file include_tongsuo.h
 * @brief Tongsuo / OpenSSL（兼容）密码库 统一依赖聚合头
 * @platform Windows/Linux/macOS
 * @desc 统一管理 Tongsuo（OpenSSL 兼容）头依赖，业务代码仅 #include "include_tongsuo.h"
 *       即可获得 EVP / HMAC / ERR / X509 / EC / BN 等密码学能力，
 *       无需在模块内散落 <openssl/...>。后续若切换底层密码库实现，
 *       仅改本文件即可，业务代码零改动。
 */
#ifndef INCLUDE_TONGSUO_H
#define INCLUDE_TONGSUO_H

// 全局统一平台宏（同源对齐所有头文件）
#if defined(_WIN32) || defined(_WIN64)
#define Q_SYS_WINDOWS
#elif defined(__linux__)
#define Q_SYS_LINUX
#elif defined(__APPLE__) && defined(__MACH__)
#define Q_SYS_MACOS
#endif

// 依赖项目基础公共头（提供 malloc/free、标准整数/内存/字符串等）
#include "include_stdio.h"

// Tongsuo / OpenSSL 密码学头（EVP 高层接口、HMAC、错误处理、X509 DER、EC、BIGNUM）
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/err.h>
#include <openssl/x509.h>   /* d2i_PUBKEY / i2d_PUBKEY（SM2 DER 密钥导入导出） */
#include <openssl/ec.h>     /* EC_KEY / EC_POINT（SM2 ECDH 派生用） */
#include <openssl/bn.h>     /* BIGNUM（SM2 ECDH 派生用） */

#endif // INCLUDE_TONGSUO_H
