/**
 * @file bizd_handler.c
 * @brief bizd 业务指令分发：外层 TLV 请求 -> JSON 信封转发下游微服务 -> 外层 TLV 响应
 *
 * 职责边界：本文件只做【协议转换与路由】
 *   - 加解密 / 帧编解码：由 q_chan / q_codec 完成；
 *   - 业务鉴权与数据落地（DB）：由下游微服务完成，本进程不持有 DB 凭据。
 *
 * 请求 id：单调递增计数器（锁保护），写入 JSON 的 "id" 字段，
 *         用于日志排障与响应关联（当前 UDS 为一请求一连，实则已天然串行）。
 */
#include "srv_bizd.h"

/* 请求 id 计数器（worker 多线程共享，需加锁） */
static uint64_t g_req_seq = 0;

static void next_req_id(char *buf, size_t cap)
{
    unsigned long long n;

    pthread_mutex_lock(&g_bizd.lock);
    n = (unsigned long long)(++g_req_seq);
    pthread_mutex_unlock(&g_bizd.lock);

    snprintf(buf, cap, "%llu", n);
}

/* 二进制 -> 小写 hex 字符串（须 free） */
static char *to_hex(const uint8_t *b, size_t len)
{
    static const char *d = "0123456789abcdef";
    char  *s = (char *)malloc(len * 2u + 1u);
    size_t i;

    if (!s)
        return NULL;
    for (i = 0u; i < len; i++)
    {
        s[i * 2u]     = d[(b[i] >> 4) & 0x0fu];
        s[i * 2 + 1u] = d[b[i] & 0x0fu];
    }
    s[len * 2u] = '\0';
    return s;
}

/* 网络序 IPv4 -> 点分十进制字符串 */
static void add_ipstr(cJSON *o, const char *k, uint32_t ip)
{
    const unsigned char *p = (const unsigned char *)&ip;
    char buf[32];

    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    cJSON_AddStringToObject(o, k, buf);
}

/* tag -> 字段名（未知返回 NULL，交由调用方按 hex 透传） */
static const char *field_name_of(uint16_t tag)
{
    switch (tag)
    {
    case Q_IPC_T_USERNAME: return "username";
    case Q_IPC_T_PWDHASH:  return "pwd_hash";
    case Q_IPC_T_OTP:      return "otp_code";
    case Q_IPC_T_DEVID:    return "device_id";
    case Q_IPC_T_CVER:     return "client_ver";
    case Q_IPC_T_AUTHTOK:  return "token";
    default:               return NULL;
    }
}

/* 内层 TLV 业务体 -> cJSON params 对象；二进制字段统一 hex 字符串承载 */
static cJSON *body_to_params(const uint8_t *body, size_t blen)
{
    cJSON *params = cJSON_CreateObject();
    size_t off    = 0u;

    if (!params)
        return NULL;

    while (off < blen)
    {
        uint16_t       tag = 0u, vlen = 0u;
        const uint8_t *val  = NULL;
        size_t         used = 0u;
        const char    *name;
        char          *hx;

        if (q_codec_tlv_decode(body + off, blen - off, &tag, &val, &vlen, &used) != 0)
            break;
        off += used;

        name = field_name_of(tag);
        hx   = to_hex(val, vlen);
        if (!hx)
            continue;

        if (name)
            cJSON_AddStringToObject(params, name, hx);
        else
        {
            char key[16];
            snprintf(key, sizeof(key), "tag_%04x", (unsigned)tag);
            cJSON_AddStringToObject(params, key, hx);
        }
        free(hx);
    }
    return params;
}

/*
 * 通用转发处理器（注册给 q_chan_disp）：
 *   body(内层 TLV) -> params(JSON) -> 下游微服务 -> {result,data} -> 外层 TLV 响应
 */
static int bizd_hnd(uint16_t cmd, const uint8_t *body, size_t body_len,
                    uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    bizd_udata_t *u = (bizd_udata_t *)ud;
    bizd_conn_t  *c;
    cJSON        *root, *ctx, *params;
    char          id[32], *req = NULL, *rsp = NULL;
    int           rc = -1;

    if (!u || !u->g || !u->c)
        return -1;
    c = u->c;

    next_req_id(id, sizeof(id));

    root = cJSON_CreateObject();
    if (!root)
        goto out;
    cJSON_AddNumberToObject(root, "ver", 1);
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddNumberToObject(root, "cmd", (double)cmd);

    ctx = cJSON_CreateObject();
    if (ctx)
    {
        char *h;

        add_ipstr(ctx, "src_ip", c->src_ip);
        h = to_hex(c->channel_id, 32u);
        if (h) { cJSON_AddStringToObject(ctx, "channel_id", h); free(h); }
        h = to_hex(c->user_id, 32u);
        if (h) { cJSON_AddStringToObject(ctx, "user_id", h); free(h); }
        h = to_hex(c->device_id, 32u);
        if (h) { cJSON_AddStringToObject(ctx, "device_id", h); free(h); }
        cJSON_AddItemToObject(root, "ctx", ctx);
    }

    {
        char *h = to_hex(c->token, 32u);
        if (h) { cJSON_AddStringToObject(root, "token", h); free(h); }
    }

    params = body_to_params(body, body_len);
    if (params)
        cJSON_AddItemToObject(root, "params", params);

    req = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!req)
        goto out;

    if (bizd_route_call(cmd, req, &rsp) == 0 && rsp)
        rc = 0;

out:
    if (req)
        free(req);
    if (rsp)
        free(rsp);

    /* 组装外层 TLV 响应：RESULT + BODY（下游 data 原样回传） */
    if (outcap >= (4u + 4u))
    {
        uint8_t rb[4];
        size_t  n = 0u;

        bizd_put_be32(rb, (rc == 0) ? (uint32_t)Q_IPC_R_OK : (uint32_t)Q_IPC_R_UNAVAILABLE);
        if (q_codec_tlv_encode(out, outcap, Q_IPC_T_RESULT, rb, 4u, &n) == 0)
            *out_len = n;
    }
    return rc;
}

int bizd_disp_init(q_chan_disp_t *d, void *ud)
{
    static const uint16_t cmds[] = {
        Q_CHAN_CMD_HELLO, Q_CHAN_CMD_LOGIN, Q_CHAN_CMD_HEARTBEAT,
        Q_CHAN_CMD_BIZ_QUERY, Q_CHAN_CMD_BIZ_UPDATE,
        Q_CHAN_CMD_VIDEO_OPEN, Q_CHAN_CMD_LOGOUT
    };
    size_t i;

    if (!d)
        return -1;

    for (i = 0u; i < sizeof(cmds) / sizeof(cmds[0]); i++)
    {
        if (q_chan_disp_register(d, cmds[i], bizd_hnd, ud) != 0)
            return -1;
    }
    return 0;
}
