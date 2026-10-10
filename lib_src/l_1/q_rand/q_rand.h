/**
 * @file q_rand.h
 * @brief l_1 基础层 安全随机数库（libqrand）对外头文件
 * @platform Windows/Linux/macOS
 * @layer  lib_src/l_1/q_rand  ->  产出 libqrand
 * @note   本文件自包含：仅依赖公共基础头 include_stdio.h（含 stdint.h），
 *         故外部模块 `#include <q_rand.h>` 可独立编译，无需依赖内部聚合头 headers.h。
 *         api/ 下源文件仍只需 `#include "headers.h"` + `#include "q_rand.h"`。
 *
 * 随机源：Linux/macOS 下读取 /dev/urandom（密码学安全熵）。后续 q_crypto（Tongsuo）
 * 实现密钥派生时依赖本模块产出随机数。
 */
#ifndef L_1_Q_RAND_H
#define L_1_Q_RAND_H

#include "include_stdio.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* 填充 len 字节高强度随机数（来源 /dev/urandom）；成功返回 0，失败 -1 */
    int q_rand_bytes(void *buf, size_t len);

    /* 随机 32/64 位无符号整数 */
    uint32_t q_rand_u32(void);
    uint64_t q_rand_u64(void);

    /* 闭区间 [min, max] 内随机整数（max<=min 时返回 min） */
    int q_rand_range(int min, int max);

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_RAND_H */
