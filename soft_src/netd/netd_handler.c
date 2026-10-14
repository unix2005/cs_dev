/**
 * @file netd_handler.c
 * @brief netd 业务指令处理器 + 登录流程（密钥协商后、认证后指令分发）
 *
 * 职责边界：本文件只做【协议语义】：状态机裁决、指令分发、Token 签发；
 * 加解密与帧编解码由 q_chan 内部完成，业务鉴权由 bizd 经 q_ipc 完成。
 */
#include "srv_netd.h"

static int hnd_hello(uint16_t cmd, const uint8_t *body, size_t body_len,
                     uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    size_t  n = 0u, off = 0u;
    uint8_t rb[4];

    (void)cmd; (void)body; (void)body_len; (void)ud;

    put_be32(rb, Q_IPC_R_OK);
    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_RESULT, rb, 4, &n) != 0)
        return -1;
    off += n;
    *out_len = off;
    return 0;
}

static int hnd_heartbeat(uint16_t cmd, const uint8_t *body, size_t body_len,
                         uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    size_t  n = 0u, off = 0u;
    uint8_t rb[4];

    (void)cmd; (void)body; (void)body_len; (void)ud;

    put_be32(rb, Q_IPC_R_OK);
    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_RESULT, rb, 4, &n) != 0)
        return -1;
    off += n;
    *out_len = off;
    return 0;
}

static int hnd_logout(uint16_t cmd, const uint8_t *body, size_t body_len,
                      uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    netd_udata_t *u = (netd_udata_t *)ud;
    size_t  n = 0u, off = 0u;
    uint8_t rb[4];

    (void)cmd; (void)body; (void)body_len;

    if (u && u->c)
    {
        u->c->has_token = 0;
        q_mem_zero(&u->c->token, sizeof(u->c->token));
    }

    put_be32(rb, Q_IPC_R_OK);
    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_RESULT, rb, 4, &n) != 0)
        return -1;
    off += n;
    *out_len = off;
    return 0;
}

/*
 * 业务指令（BIZ_QUERY / BIZ_UPDATE / VIDEO_OPEN）：转 bizd，回包原样带回。
 * 客户端把会话 Token 放在业务体 TLV(Q_IPC_T_TOKEN) 内随请求带回；
 * netd 连同 channel_id / user_id / src_ip 一并转给 bizd 做绑定校验
 * （§6.3：Token 仅出现在密文体内）。
 */
static int hnd_biz(uint16_t cmd, const uint8_t *body, size_t body_len,
                   uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    netd_udata_t *u = (netd_udata_t *)ud;
    uint32_t result = Q_IPC_R_UNAVAILABLE;
    size_t   n = 0u, off = 0u;
    uint8_t  rb[4];

    if (!u || !u->g || !u->c)
        return -1;

    if (ipc_forward(u->g, &u->c->src_addr, cmd, body, body_len,
                    u->c->channel_id, u->c->token.user_id,
                    &result, out, outcap, out_len) != 0)
    {
        /* bizd 不可用：回带结果码的响应，避免客户端空等 */
        put_be32(rb, Q_IPC_R_UNAVAILABLE);
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_RESULT, rb, 4, &n) == 0)
        {
            off += n;
            *out_len = off;
        }
        return 0;
    }
    return (result == Q_IPC_R_OK) ? 0 : -1;
}

/* 把本进程支持的指令注册到分发表；ud 透传给各 handler */
int netd_disp_init(q_chan_disp_t *d, void *ud)
{
    if (!d)
        return -1;

    if (q_chan_disp_register(d, Q_CHAN_CMD_HELLO,      hnd_hello,     ud) != 0)
        return -1;
    if (q_chan_disp_register(d, Q_CHAN_CMD_HEARTBEAT,   hnd_heartbeat, ud) != 0)
        return -1;
    if (q_chan_disp_register(d, Q_CHAN_CMD_BIZ_QUERY,  hnd_biz,       ud) != 0)
        return -1;
    if (q_chan_disp_register(d, Q_CHAN_CMD_BIZ_UPDATE, hnd_biz,       ud) != 0)
        return -1;
    if (q_chan_disp_register(d, Q_CHAN_CMD_VIDEO_OPEN, hnd_biz,       ud) != 0)
        return -1;
    if (q_chan_disp_register(d, Q_CHAN_CMD_LOGOUT,      hnd_logout,    ud) != 0)
        return -1;

    return 0;
}

/* 从 LOGIN 业务体解析 username / device_id，构造绑定因子（§6.2 / §6.3） */
static void login_bind_ids(netd_conn_t *c, const uint8_t *body, size_t blen)
{
    size_t off = 0u;

    q_mem_zero(&c->token, sizeof(c->token));
    memcpy(c->token.channel_id, c->channel_id, Q_CHAN_CHANID_LEN);

    while (off < blen)
    {
        uint16_t tag = 0u, vlen = 0u;
        const uint8_t *val = NULL;
        size_t used = 0u;

        if (q_codec_tlv_decode(body + off, blen - off, &tag, &val, &vlen, &used) != 0)
            break;
        off += used;

        if (tag == Q_IPC_T_USERNAME && vlen > 0u)
            (void)q_crypto_sm3(val, vlen, c->token.user_id);     /* user_id = SM3(用户名) */
        else if (tag == Q_IPC_T_DEVID && vlen > 0u)
            memcpy(c->token.device_id, val, vlen > Q_CHAN_DEVID_LEN ? Q_CHAN_DEVID_LEN : vlen);
    }
}

/*
 * LOGIN 处理：把凭据转给 bizd 校验，成功则签发会话 Token。
 * 返回 1 登录成功、0 失败（由调用方交状态机裁决 EV_LOGIN_OK / EV_LOGIN_FAIL）。
 */
int do_login(netd_conn_t *c, const uint8_t *body, size_t blen,
             uint8_t *out, size_t outcap, size_t *out_len)
{
    uint32_t result = Q_IPC_R_UNAVAILABLE;

    login_bind_ids(c, body, blen);

    if (ipc_forward(c->g, &c->src_addr, Q_CHAN_CMD_LOGIN, body, blen,
                    c->channel_id, c->token.user_id,
                    &result, out, outcap, out_len) != 0)
    {
        /* bizd 不可用：仍回带结果码的响应，避免客户端空等 */
        size_t n = 0u, off = 0u;
        uint8_t rb[4];

        put_be32(rb, Q_IPC_R_UNAVAILABLE);
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_RESULT, rb, 4, &n) == 0)
        {
            off += n;
            *out_len = off;
        }
        return 0;
    }

    if (result != Q_IPC_R_OK)
        return 0;

    /* 签发会话 Token，绑定 channel_id + user_id + device_id + src_ip（§6.3）。
       token.src_ip 仍为 uint32 IPv4（§6.4 约定；IPv6 客户端此处记 0）。 */
    if (q_chan_token_issue(c->channel_id, c->token.user_id, c->token.device_id,
                           netd_addr_v4(&c->src_addr), NETD_TOKEN_TTL_SEC, &c->token) != 0)
        return 0;

    c->has_token = 1;
    {
        size_t n = 0u;
        if (q_codec_tlv_encode(out + *out_len, outcap - *out_len,
                               Q_IPC_T_TOKEN, c->token.token,
                               Q_CHAN_TOKEN_LEN, &n) == 0)
            *out_len += n;
    }
    return 1;
}
