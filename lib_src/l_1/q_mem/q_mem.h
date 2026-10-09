/**
 * @file q_mem.h
 * @brief l_1 基础层 内存库（libqmem）对外头文件
 * @platform Windows/Linux/macOS
 * @layer  lib_src/l_1/q_mem  → 产出 libqmem
 * @note   本文件自包含：直接引入所需公共基础头（include_stdio.h），
 *         故外部模块 `#include <q_mem.h>` 可独立编译，无需依赖内部聚合头 headers.h。
 *         api/ 下源文件仍只需 `#include "headers.h"` + `#include "q_mem.h"`。
 *
 * 职责（依据 AI_SKILL_RULE §3.1 l_1 基础层）：
 *   - 安全清零：敏感数据（密钥 / OTP / Token）使用完毕立即安全擦除，防内存残留泄露
 *   - 内存池：固定块池，销毁时一次性安全擦除整片区域（适合集中保管短期敏感缓冲）
 *   - 动态缓冲区：可增长字节缓冲，清空/销毁均安全擦除内容
 *
 * 安全约束（依据 §3.2.4 / §3.2.10）：
 *   - 密钥、临时敏感数据使用完毕必须调用 q_mem_zero / q_mempool_clear / q_buf_clear 清零
 *   - 本库为 l_1 基础层，零项目内依赖，仅依赖 libc / OS syscall
 *
 * 线程安全：q_mempool_t / q_buf_t 本身不做内部加锁，非线程安全；
 *          多线程场景由调用方保证串行访问（推荐每线程独立池/缓冲）。
 */
#ifndef L_1_Q_MEM_H
#define L_1_Q_MEM_H

/* 本文件自包含：直接引入所需公共基础头，便于外部模块独立编译。
   （feature 宏 _POSIX_C_SOURCE/_DEFAULT_SOURCE 由 api/ 源文件的 headers.h 统一定义，
    本公共头不重复定义，避免外部 TU 重复包含时触发 redefine 告警） */
#include "include_stdio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ============ 内部结构（公开布局，便于外部只读访问 len/cap 等） ============ */

    struct q_mempool
    {
        size_t block_size;   /* 对齐后的块大小（字节） */
        size_t block_count;  /* 总块数 */
        size_t free_top;     /* 空闲栈顶（<= block_count） */
        unsigned char *base; /* 池内存起始（已按块对齐） */
        void **free_list;    /* 空闲块指针栈，容量 block_count */
    };
    typedef struct q_mempool q_mempool_t;

    struct q_buf
    {
        unsigned char *data;
        size_t len;
        size_t cap;
    };
    typedef struct q_buf q_buf_t;

    /* ===================== A. 安全清零 ===================== */

    /**
     * @brief 安全清零：防止编译器把“清零敏感数据”优化掉（基于 explicit_bzero，
     *        或等价的可移植易失写；Windows 下使用 SecureZeroMemory）
     * @param buf  缓冲区；NULL 安全
     * @param n    字节数；0 安全
     */
    void q_mem_zero(void *buf, size_t n);

    /**
     * @brief 安全释放：先安全擦除 n 字节再 free（p 为 NULL 时仅 free）
     * @note  适用于“单独 malloc 的敏感缓冲”；池内/缓冲内对象请用对应 clear/destroy
     */
    void q_mem_secure_free(void *p, size_t n);

    /* ===================== B. 内存池（固定块，安全擦除） ===================== */

    /**
     * @brief 创建固定块内存池
     * @param block_size   每块逻辑大小（内部向上对齐到 16 字节，满足国密对齐习惯）
     * @param block_count  块数量
     * @return             池句柄，分配失败返回 NULL
     * @note  整片区域一次性 malloc；后续 q_mempool_destroy 会先安全擦除再释放
     */
    q_mempool_t *q_mempool_create(size_t block_size, size_t block_count);

    /**
     * @brief 分配一块（大小固定为对齐后的 block_size），池满返回 NULL
     */
    void *q_mempool_alloc(q_mempool_t *mp);

    /**
     * @brief 归还一块到池（校验归属与重复归还；非法/越界指针被安全忽略）
     */
    void q_mempool_free(q_mempool_t *mp, void *p);

    /**
     * @brief 安全擦除池内所有已分配块的内容（不释放池本身）
     */
    void q_mempool_clear(q_mempool_t *mp);

    /**
     * @brief 销毁：先安全擦除整片区域再 free
     */
    void q_mempool_destroy(q_mempool_t *mp);

    /**
     * @brief 当前空闲块数（用于 ut 校验与容量规划）
     */
    size_t q_mempool_avail(const q_mempool_t *mp);

    /* ===================== C. 动态缓冲区 ===================== */

    /**
     * @brief 创建动态缓冲区，初始容量 cap（传 0 时使用默认下限 64 字节懒惰分配）
     * @return 句柄，失败返回 NULL
     */
    q_buf_t *q_buf_create(size_t cap);

    /**
     * @brief 追加数据；空间不足自动按 1.5x 增长，增长后对新扩容区间立即安全清零（防堆残留）
     * @return 0 成功，-1 参数非法或内存不足
     */
    int q_buf_append(q_buf_t *b, const void *data, size_t len);

    /** 当前有效长度（字节）；b 为 NULL 时返回 0 */
    size_t q_buf_len(const q_buf_t *b);

    /** 只读数据指针（len==0 或 b 为 NULL 时返回 NULL） */
    const void *q_buf_data(const q_buf_t *b);

    /** 安全擦除内容并清零长度（保留已分配容量，便于复用） */
    void q_buf_clear(q_buf_t *b);

    /** 安全擦除内容并释放 */
    void q_buf_destroy(q_buf_t *b);

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_MEM_H */
