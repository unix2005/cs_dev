/**
 * @file qc_push_wire.h
 * @brief l_4/q_clientcomm 【内部头】：M5 推送载荷的 wire 编解码小工具
 * @platform Windows / Linux / macOS
 * @layer  lib_src/l_4/q_clientcomm（模块内共享，不对外导出）
 *
 * 为什么单独一层：推送载荷（SUB_DELTA 请求体 / TRAJ / ALARM）的 TLV tag 定义在
 * include_ipc_proto.h（netd<->pushd<->客户端三处共用的事实来源），而"字节序怎么摆"
 * 必须三端一致 —— double 用 IEEE754 位型大端、整数用大端、文本 UTF-8 无 NUL。
 * 一端偷懒用本机序，另一端解出来就是天文数字，且**不报错**，故收敛成一组函数。
 */
#ifndef QC_PUSH_WIRE_H
#define QC_PUSH_WIRE_H

#include "qc_push.h"
#include "q_codec.h"          /* q_codec_tlv_encode / q_codec_tlv_decode */
#include "include_ipc_proto.h" /* Q_IPC_T_PUSH_* / Q_PUSH_OP_* */

#include <stdint.h>
#include <stddef.h>

/* double <-> IEEE754 位型大端（8B）。坐标必须双精度：GCJ-02 单精度在百万级
   经度上误差约 1 米量级且不可累积修正，故明确走 8B 位型而非文本。 */
/* double -> IEEE754 位型**值**（不是字节序转换！memcpy 到 uint64 再读值，
   大/小端主机上都得到正确的位型值）。上线时由 qc_push_put_f64 按大端逐字节移出；
   切勿在这里手工挪字节 —— 会把"值"和"线格式"两件事搅在一起，正是字节序 bug 的源头。 */
static inline uint64_t qc_push_f64_be(double d)
{
    uint64_t u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

/* 上式逆运算：位型值 -> double。 */
static inline double qc_push_be_f64(uint64_t be)
{
    double d;
    memcpy(&d, &be, sizeof(d));
    return d;
}

/* 追加一条 TLV；cap 为 out 剩余容量。成功 0，失败 -1（缓冲不足）。 */
int qc_push_tlv_put(uint8_t *out, size_t cap, size_t *off,
                    uint16_t tag, const void *val, uint16_t vlen);

/* 大端序列化便捷版：内部先把值转成 wire 字节再 tlv_put。
   **必须用这组函数而不是直接传 &u/&d** —— TLV 拷的是原始字节，
   直接传本机序指针在小端主机上等于把字节序悄悄写反，且不报错。 */
int qc_push_put_u32(uint8_t *out, size_t cap, size_t *off,
                    uint16_t tag, uint32_t v);
int qc_push_put_f64(uint8_t *out, size_t cap, size_t *off,
                    uint16_t tag, double d);

/* 组一帧 PUSH_SUB_DELTA 明文体：TLV(PUSH_OP) + TLV(PUSH_VID)*n。
   ids 中过长/为空的键跳过（绝不静默截断成半个主键）。返回实际字节数；-1 失败。 */
int qc_push_build_delta(uint8_t *out, size_t cap, int op,
                        const char *const *ids, uint32_t n, size_t *blen);

/* 解析 TRAJ 明文体 -> qc_traj_t。返回 0；-1 结构非法（缺 vehicle_id 等必填项）。 */
int qc_push_parse_traj(const uint8_t *body, size_t blen, qc_traj_t *out);

/* 解析 ALARM 明文体 -> qc_alarm_t。返回 0；-1 结构非法。 */
int qc_push_parse_alarm(const uint8_t *body, size_t blen, qc_alarm_t *out);

#endif /* QC_PUSH_WIRE_H */
