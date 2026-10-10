/**
 * @file q_xcfg.h
 * @brief l_1 基础层 XML 配置库（libqxcfg）对外头文件
 * @platform Windows/Linux/macOS
 * @layer  lib_src/l_1/q_xcfg  ->  产出 libqxcfg
 * @note   本文件自包含：仅依赖公共基础头 include_stdio.h（提供 bool/stdint 等），
 *         故外部模块 `#include <q_xcfg.h>` 可独立编译，无需依赖内部聚合头 headers.h。
 *         api/ 下源文件仍只需 `#include "headers.h"` + `#include "q_xcfg.h"`。
 *
 * 职责（依据 AI_SKILL_RULE §3.1 l_1 基础层）：
 *   - 配置读取接口：内部借助 libxml2 的 XPath 引擎，将"点分键"（如 service.port）
 *     转换为 XPath 表达式（/service/port/text()）后求值。
 *   - 所有公开函数对 NULL 上下文/键均做防御性处理，并返回默认值或 NULL。
 *   - 句柄（q_xcfg_ctx_t）为不透明类型，调用方无需感知 libxml2 细节。
 */
#ifndef L_1_Q_XCFG_H
#define L_1_Q_XCFG_H

/* 仅引入最基础公共头，保证本头自包含且不向外部泄漏 XML 内部依赖 */
#include "include_stdio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* 不透明配置上下文（实现细节见 api/q_xcfg_internal.h，外部不可见） */
    typedef struct q_xcfg q_xcfg_ctx_t;

    /**
     * @brief 打开并解析 XML 配置文件
     * @param config_file XML 配置文件路径
     * @return 配置上下文，失败返回 NULL
     */
    q_xcfg_ctx_t *q_xcfg_init(const char *config_file);

    /** 释放配置上下文 */
    void q_xcfg_destroy(q_xcfg_ctx_t *ctx);

    /**
     * @brief 读取整数配置
     * @param key 配置键（支持点分路径，如 "gateway.port"）
     * @param default_value 默认值（键缺失或解析失败时返回）
     */
    int q_xcfg_get_int(q_xcfg_ctx_t *ctx, const char *key, int default_value);

    /**
     * @brief 读取字符串配置（返回调用者须 free 的副本）
     * @return 字符串副本，缺失时返回 default_value 的副本（default_value 为 NULL 则返回 NULL）
     */
    char *q_xcfg_get_string(q_xcfg_ctx_t *ctx, const char *key, const char *default_value);

    /** 读取布尔配置（支持 true/yes/1，其余为 false） */
    bool q_xcfg_get_bool(q_xcfg_ctx_t *ctx, const char *key, bool default_value);

    /** 读取浮点数配置 */
    double q_xcfg_get_float(q_xcfg_ctx_t *ctx, const char *key, double default_value);

    /** 检查配置键是否存在 */
    bool q_xcfg_has_key(q_xcfg_ctx_t *ctx, const char *key);

    /** 打印全部配置（调试用，输出到 stdout） */
    void q_xcfg_dump(q_xcfg_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_XCFG_H */
