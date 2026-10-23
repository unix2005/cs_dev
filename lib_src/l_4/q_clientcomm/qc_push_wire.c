/**
 * @file qc_push_wire.c
 * @brief M5 推送载荷 wire 编解码实现（TLV 组帧 / TRAJ / ALARM 解析）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * tag 与字节序契约见 include_ipc_proto.h 的 Q_IPC_T_PUSH_* 段；本文件只实现
 * "键值 -> 字节" 与 "字节 -> 键值" 的机械映射，不含任何业务判断。
 */
#include "qc_push_wire.h"

#include <string.h>

int qc_push_tlv_put(uint8_t *out, size_t cap, size_t *off,
                    uint16_t tag, const void *val, uint16_t vlen)
{
    size_t n = 0;
    if (!out || !off || *off > cap)
        return -1;
    if (q_codec_tlv_encode(out + *off, cap - *off, tag,
                           (const uint8_t *)val, vlen, &n) != 0)
        return -1;
    *off += n;
    return 0;
}

int qc_push_put_u32(uint8_t *out, size_t cap, size_t *off,
                    uint16_t tag, uint32_t v)
{
    uint8_t w[4];
    w[0] = (uint8_t)(v >> 24);
    w[1] = (uint8_t)(v >> 16);
    w[2] = (uint8_t)(v >> 8);
    w[3] = (uint8_t)(v);
    return qc_push_tlv_put(out, cap, off, tag, w, 4);
}

int qc_push_put_f64(uint8_t *out, size_t cap, size_t *off,
                    uint16_t tag, double d)
{
    uint64_t be = qc_push_f64_be(d);
    uint8_t  w[8];
    int      i;
    for (i = 0; i < 8; ++i)
        w[i] = (uint8_t)(be >> (56 - 8 * i));
    return qc_push_tlv_put(out, cap, off, tag, w, 8);
}

int qc_push_build_delta(uint8_t *out, size_t cap, int op,
                        const char *const *ids, uint32_t n, size_t *blen)
{
    uint8_t opb = (uint8_t)op;
    size_t  off = 0;
    uint32_t i;

    if (!out || !ids || !blen)
        return -1;
    if (op != Q_PUSH_OP_ADD && op != Q_PUSH_OP_REMOVE)
        return -1;
    *blen = 0;

    if (qc_push_tlv_put(out, cap, &off, Q_IPC_T_PUSH_OP, &opb, 1) != 0)
        return -1;

    for (i = 0; i < n; ++i)
    {
        size_t len;
        if (!ids[i] || !ids[i][0])
            continue;                       /* 空键跳过，不发半个主键 */
        len = strlen(ids[i]);
        if (len == 0u || len >= QC_PUSH_KEY_LEN)
            continue;                       /* 超长键跳过：绝不静默截断 */
        if (qc_push_tlv_put(out, cap, &off, Q_IPC_T_PUSH_VID, ids[i],
                            (uint16_t)len) != 0)
            return -1;                      /* 缓冲不足：宁可整帧失败 */
    }
    *blen = off;
    return 0;
}

/* 定长文本拷贝：保证 NUL 结尾；过长截断（调用方已按 QC_PUSH_KEY_LEN 过滤，这里双保险） */
static void copy_text(char *dst, size_t cap, const uint8_t *src, uint16_t len)
{
    size_t n = len;
    if (n >= cap)
        n = cap - 1u;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* 从 TLV 序列取 uint32 大端；找不到返回 def */
static uint32_t get_u32(const uint8_t *body, size_t blen, uint16_t tag, uint32_t def)
{
    size_t off = 0;
    while (off + 4u <= blen)
    {
        uint16_t t;
        const uint8_t *v;
        uint16_t vlen;
        size_t used;
        if (q_codec_tlv_decode(body + off, blen - off, &t, &v, &vlen, &used) != 0)
            break;
        off += used;
        if (t == tag && vlen >= 4u)
            return ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                   ((uint32_t)v[2] << 8) | (uint32_t)v[3];
    }
    return def;
}

/* 从 TLV 序列取文本（第一个匹配项）；找不到返回 def */
static void get_text(const uint8_t *body, size_t blen, uint16_t tag,
                     char *dst, size_t cap, const char *def)
{
    size_t off = 0;
    while (off + 4u <= blen)
    {
        uint16_t t;
        const uint8_t *v;
        uint16_t vlen;
        size_t used;
        if (q_codec_tlv_decode(body + off, blen - off, &t, &v, &vlen, &used) != 0)
            break;
        off += used;
        if (t == tag)
        {
            copy_text(dst, cap, v, vlen);
            return;
        }
    }
    if (def)
        copy_text(dst, cap, (const uint8_t *)def, (uint16_t)strlen(def));
    else if (cap)
        dst[0] = '\0';
}

/* 取 8B 大端 double；找不到返回 def */
static double get_f64(const uint8_t *body, size_t blen, uint16_t tag, double def)
{
    size_t off = 0;
    while (off + 4u <= blen)
    {
        uint16_t t;
        const uint8_t *v;
        uint16_t vlen;
        size_t used;
        if (q_codec_tlv_decode(body + off, blen - off, &t, &v, &vlen, &used) != 0)
            break;
        off += used;
        if (t == tag && vlen >= 8u)
        {
            uint64_t be = 0;
            int i;
            for (i = 0; i < 8; ++i)
                be = (be << 8) | (uint64_t)v[i];
            return qc_push_be_f64(be);
        }
    }
    return def;
}

int qc_push_parse_traj(const uint8_t *body, size_t blen, qc_traj_t *out)
{
    if (!body || !out || blen == 0u)
        return -1;
    memset(out, 0, sizeof(*out));

    /* vehicle_id 是必填项：没有它连"这辆车是谁"都无法回答，直接判非法 */
    get_text(body, blen, Q_IPC_T_PUSH_VID, out->vehicle_id,
             sizeof(out->vehicle_id), NULL);
    if (!out->vehicle_id[0])
        return -1;

    out->lon         = get_f64(body, blen, Q_IPC_T_PUSH_LON, 0.0);
    out->lat         = get_f64(body, blen, Q_IPC_T_PUSH_LAT, 0.0);
    out->speed_kph   = get_u32(body, blen, Q_IPC_T_PUSH_SPEED, 0u);
    out->online      = get_u32(body, blen, Q_IPC_T_PUSH_ONLINE, 1u);
    out->alarm_flags = get_u32(body, blen, Q_IPC_T_PUSH_AFLAGS, 0u);
    get_text(body, blen, Q_IPC_T_PUSH_TV, out->time, sizeof(out->time), "");
    get_text(body, blen, Q_IPC_T_PUSH_ADDR, out->address,
             sizeof(out->address), "");
    return 0;
}

int qc_push_parse_alarm(const uint8_t *body, size_t blen, qc_alarm_t *out)
{
    if (!body || !out || blen == 0u)
        return -1;
    memset(out, 0, sizeof(*out));

    get_text(body, blen, Q_IPC_T_PUSH_VID, out->vehicle_id,
             sizeof(out->vehicle_id), NULL);
    if (!out->vehicle_id[0])
        return -1;                          /* 不知道车的报警没有意义 */

    get_text(body, blen, Q_IPC_T_PUSH_ORG, out->org_id, sizeof(out->org_id), "");
    get_text(body, blen, Q_IPC_T_PUSH_PLATE, out->plate, sizeof(out->plate), "");
    out->alarm_type = get_u32(body, blen, Q_IPC_T_PUSH_ATYPE, 0u);
    out->lon        = get_f64(body, blen, Q_IPC_T_PUSH_LON, 0.0);
    out->lat        = get_f64(body, blen, Q_IPC_T_PUSH_LAT, 0.0);
    get_text(body, blen, Q_IPC_T_PUSH_TV, out->time, sizeof(out->time), "");
    get_text(body, blen, Q_IPC_T_PUSH_ATEXT, out->text, sizeof(out->text), "");
    get_text(body, blen, Q_IPC_T_PUSH_ADDR, out->address,
             sizeof(out->address), "");
    return 0;
}
