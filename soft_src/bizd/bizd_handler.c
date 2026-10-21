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

/* hex 字符 -> 值（非法返回 -1） */
static int bizd_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex 字符串 -> 原始字节；out 容量需 >= hlen/2；返回字节数，失败 -1 */
static int bizd_from_hex(const char *hex, size_t hlen, uint8_t *out, size_t outcap)
{
    size_t i, n = hlen / 2u;

    if (hlen % 2u != 0u || n > outcap)
        return -1;
    for (i = 0u; i < n; i++)
    {
        int hi = bizd_hexval(hex[2u * i]);
        int lo = bizd_hexval(hex[2u * i + 1u]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

/* 客户端源地址（v4/v6 统一）-> 字符串，写入 JSON 的 ctx.src_ip */
static void add_ipstr(cJSON *o, const char *k, uint8_t fam, const uint8_t *addr)
{
    char buf[INET6_ADDRSTRLEN];

    if (fam == 6u)
        inet_ntop(AF_INET6, addr, buf, sizeof(buf));
    else
        inet_ntop(AF_INET, addr, buf, sizeof(buf));
    cJSON_AddStringToObject(o, k, buf);
}

/* 内层 TLV 字段在下游 JSON 里的承载方式 */
typedef enum
{
    FIELD_HEX  = 0,   /* 二进制 -> hex 字符串（凭据类，下游按 hex 解码） */
    FIELD_TEXT,       /* UTF-8 文本原样（业务子类型 / 参数） */
    FIELD_U32         /* uint32 大端 -> JSON number */
} field_kind_t;

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
    /* —— 业务参数段（M2）—— */
    case Q_IPC_T_BIZ_TYPE:    return "biz_type";
    case Q_IPC_T_BIZ_PARAM:   return "biz_param";
    case Q_IPC_T_PAGE_OFFSET: return "offset";
    case Q_IPC_T_PAGE_LIMIT:  return "limit";
    default:               return NULL;
    }
}

/* tag -> 承载方式：0x01xx 凭据类保持 hex（登录服务已按 hex 解析，改了就是协议漂移）；
   0x02xx 业务段按语义传（文本原样 / uint32 数值），避免下游再解一层 hex。 */
static field_kind_t field_kind_of(uint16_t tag)
{
    switch (tag)
    {
    case Q_IPC_T_BIZ_TYPE:
    case Q_IPC_T_BIZ_PARAM:
        return FIELD_TEXT;
    case Q_IPC_T_PAGE_OFFSET:
    case Q_IPC_T_PAGE_LIMIT:
        return FIELD_U32;
    default:
        return FIELD_HEX;
    }
}

/* 内层 TLV 业务体 -> cJSON params 对象 */
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
        field_kind_t   kind;
        char           key[16];
        char          *hx;

        if (q_codec_tlv_decode(body + off, blen - off, &tag, &val, &vlen, &used) != 0)
            break;
        off += used;

        name = field_name_of(tag);
        kind = field_kind_of(tag);
        if (!name)
        {
            snprintf(key, sizeof(key), "tag_%04x", (unsigned)tag);
            name = key;
        }

        if (kind == FIELD_U32)
        {
            /* 长度必须是 4；不对则丢弃该字段（不把畸形值透给下游） */
            if (vlen == 4u)
                cJSON_AddNumberToObject(params, name,
                                        (double)(((uint32_t)val[0] << 24) |
                                                 ((uint32_t)val[1] << 16) |
                                                 ((uint32_t)val[2] << 8)  |
                                                 (uint32_t)val[3]));
            continue;
        }

        if (kind == FIELD_TEXT)
        {
            /* 文本型拒绝内嵌 NUL：cJSON 按 strlen 取串，内嵌 NUL 会造成"截断欺骗"
               （下游看到的值与签名/审计看到的字节不一致），故整字段丢弃。 */
            char *s;
            if (vlen == 0u || memchr(val, 0, vlen))
                continue;
            s = (char *)malloc((size_t)vlen + 1u);
            if (!s)
                continue;
            memcpy(s, val, vlen);
            s[vlen] = '\0';
            cJSON_AddStringToObject(params, name, s);
            free(s);
            continue;
        }

        hx = to_hex(val, vlen);
        if (!hx)
            continue;
        cJSON_AddStringToObject(params, name, hx);
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
    char         *req = NULL, *rsp = NULL;
    int           rc = -1;

    if (!u || !u->g || !u->c)
        return -1;
    c = u->c;

    /* 请求号优先采用 netd 经 IPC REQID TLV 透传的值（整条链一致），
       仅在缺失时（如直接测试未带 REQID）回退到本地生成。 */
    if (c->req_id[0] == '\0')
        next_req_id(c->req_id, sizeof(c->req_id));

    root = cJSON_CreateObject();
    if (!root)
        goto out;
    cJSON_AddNumberToObject(root, "ver", 1);
    cJSON_AddStringToObject(root, "id", c->req_id);
    cJSON_AddNumberToObject(root, "cmd", (double)cmd);

    ctx = cJSON_CreateObject();
    if (ctx)
    {
        char *h;

        add_ipstr(ctx, "src_ip", c->src_fam, c->src_ip);
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

    /* 组装外层 TLV 响应：RESULT [+ BODY（下游响应原样回传，netd 透传给客户端）] */
    {
        size_t  off = 0u, n = 0u;
        uint8_t rb[4];

        bizd_put_be32(rb, (rc == 0) ? (uint32_t)Q_IPC_R_OK : (uint32_t)Q_IPC_R_UNAVAILABLE);
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_RESULT, rb, 4u, &n) == 0)
        {
            off += n;
            if (rc == 0 && rsp)
            {
                size_t blen = strlen(rsp);
                if (blen > Q_IPC_TLV_MAX)
                    blen = Q_IPC_TLV_MAX;
                if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_BODY,
                                       (const uint8_t *)rsp, (uint16_t)blen, &n) == 0)
                    off += n;
            }
            *out_len = off;
        }
    }

    if (rsp)
        free(rsp);
    return rc;
}

/*
 * SALT_QUERY 处理（登录前拉取账号 salt，方案A）：
 *   请求体 TLV(USERNAME) -> JSON params {username} -> 下游 auth 微服务；
 *   响应 JSON {result,data:{salt:"<hex>"}} -> 转原始字节装入 Q_IPC_T_BODY 回传
 *   （客户端 TLV-only，不解析 JSON）。
 */
static int bizd_hnd_salt(uint16_t cmd, const uint8_t *body, size_t body_len,
                         uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    bizd_udata_t *u = (bizd_udata_t *)ud;
    bizd_conn_t  *c;
    cJSON        *root = NULL, *params = NULL, *rsp_json = NULL;
    cJSON        *rj = NULL, *data = NULL, *salt_obj = NULL;
    char         *req = NULL, *rsp = NULL;
    size_t        off = 0u, n = 0u;
    uint8_t       rb[4];
    uint8_t       raw[64];            /* salt 原始字节（上限 64B） */
    int           rawlen = 0;         /* >0 表示成功取到 salt */
    uint32_t      result = Q_IPC_R_UNAVAILABLE;
    int           rc = -1;

    if (!u || !u->g || !u->c)
        return -1;
    c = u->c;
    if (c->req_id[0] == '\0')
        next_req_id(c->req_id, sizeof(c->req_id));

    root = cJSON_CreateObject();
    if (!root)
        goto out;
    cJSON_AddNumberToObject(root, "ver", 1);
    cJSON_AddStringToObject(root, "id", c->req_id);
    cJSON_AddNumberToObject(root, "cmd", (double)cmd);
    params = body_to_params(body, body_len);   /* USERNAME TLV -> {"username":"..."} */
    if (params)
        cJSON_AddItemToObject(root, "params", params);

    req = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    root = NULL;
    if (!req)
        goto out;

    if (bizd_route_call(cmd, req, &rsp) != 0 || !rsp)
    {
        result = Q_IPC_R_UNAVAILABLE;   /* 下游不可达：仍回 RESULT，避免客户端空等至超时 */
        rc = 0;
        goto out;
    }

    rsp_json = cJSON_Parse(rsp);
    if (!rsp_json)
    {
        result = Q_IPC_R_INTERNAL;
        rc = 0;
        goto out;
    }

    rj = cJSON_GetObjectItem(rsp_json, "result");
    if (!rj || rj->type != cJSON_Number)
    {
        result = Q_IPC_R_INTERNAL;
        rc = 0;
        goto out;
    }
    result = (uint32_t)(rj->valuedouble);
    rc = 0;                             /* 下游已应答：结果码原样透传，不视为转发失败 */
    if (result != (uint32_t)Q_IPC_R_OK)
        goto out;

    data     = cJSON_GetObjectItem(rsp_json, "data");
    salt_obj = data ? cJSON_GetObjectItem(data, "salt") : NULL;
    if (salt_obj && salt_obj->type == cJSON_String && salt_obj->valuestring)
    {
        const char *hex  = salt_obj->valuestring;
        size_t      hlen = strlen(hex);

        if (hlen / 2u > 0u && hlen / 2u <= sizeof(raw))
            rawlen = bizd_from_hex(hex, hlen, raw, sizeof(raw));
    }
    if (rawlen <= 0)
        result = Q_IPC_R_BADREQ;        /* 下游没给 salt：显式报错，不要回空响应 */

out:
    /*
     * 统一出口：**任何**路径都必须回带 RESULT —— 否则 netd 透传空响应，
     * 客户端会一直阻塞到 socket 超时（30s）才失败，排障时表现为「登录卡死」。
     */
    bizd_put_be32(rb, result);
    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_RESULT, rb, 4u, &n) == 0)
    {
        off += n;
        if (result == (uint32_t)Q_IPC_R_OK && rawlen > 0)
        {
            if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_BODY,
                                   raw, (uint16_t)rawlen, &n) == 0)
                off += n;
        }
        *out_len = off;
    }

    if (rsp_json)
        cJSON_Delete(rsp_json);
    if (req)
        free(req);
    if (rsp)
        free(rsp);
    if (root)
        cJSON_Delete(root);
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
    /* SALT_QUERY 需要专用 handler：把下游 salt hex 转原始字节装入 BODY（客户端 TLV-only） */
    if (q_chan_disp_register(d, Q_CHAN_CMD_SALT_QUERY, bizd_hnd_salt, ud) != 0)
        return -1;
    return 0;
}
