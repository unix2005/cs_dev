/**
 * @file netd_handler.c
 * @brief netd 业务指令处理器 + 登录流程（密钥协商后、认证后指令分发）
 *
 * 职责边界：本文件只做【协议语义】：状态机裁决、指令分发、Token 签发；
 * 加解密与帧编解码由 q_chan 内部完成，业务鉴权由 bizd 经 q_ipc 完成。
 */
#include "srv_netd.h"

/** 把 RESULT 结果码编码进 out（netd 各处「回带结果码」统一走这里，避免 tag/长度漂移） */
static void netd_put_result(uint8_t *out, size_t outcap, size_t *off, uint32_t result)
{
    uint8_t rb[4];
    size_t  n = 0u;

    put_be32(rb, result);
    if (q_codec_tlv_encode(out + *off, outcap - *off, Q_IPC_T_RESULT, rb, 4, &n) == 0)
        *off += n;
}

static int hnd_hello(uint16_t cmd, const uint8_t *body, size_t body_len,
                     uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    size_t  off = 0u;

    (void)cmd; (void)body; (void)body_len; (void)ud;

    /* HELLO 由 netd 本地应答（能力协商，不下发 bizd）：无需下游微服务即可连通，
       三端客户端在握手后第一条指令即 HELLO，此处必须自洽返回 RESULT=OK。 */
    netd_put_result(out, outcap, &off, Q_IPC_R_OK);
    *out_len = off;
    return 0;
}

static int hnd_heartbeat(uint16_t cmd, const uint8_t *body, size_t body_len,
                         uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    size_t  off = 0u;

    (void)cmd; (void)body; (void)body_len; (void)ud;

    /* 心跳仅本地保活；注意：此处【不】重新签发 Token，Token TTL 到期后客户端须重新登录 */
    netd_put_result(out, outcap, &off, Q_IPC_R_OK);
    *out_len = off;
    return 0;
}

static int hnd_logout(uint16_t cmd, const uint8_t *body, size_t body_len,
                      uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    netd_udata_t *u = (netd_udata_t *)ud;
    size_t   off = 0u;

    (void)cmd; (void)body; (void)body_len;

    if (u && u->c)
    {
        u->c->has_token = 0;
        q_mem_zero(&u->c->token, sizeof(u->c->token));
    }

    netd_put_result(out, outcap, &off, Q_IPC_R_OK);
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
    size_t   off = 0u;
    char     cid[INET6_ADDRSTRLEN + 128];

    if (!u || !u->g || !u->c)
        return -1;

    QLOG_INFO(u->g->log, "转发业务请求 %s cmd=%u -> bizd",
              netd_cid(u->c, cid, sizeof(cid)), (unsigned)cmd);

    /* Token 透传：bizd 只从外层 Q_IPC_T_TOKEN 取值转给下游，
       已登录的业务请求必须带上，否则下游 token 恒为全零、鉴权形同虚设。 */
    if (ipc_forward(u->g, &u->c->src_addr, cmd, body, body_len,
                    u->c->channel_id, u->c->token.user_id,
                    u->c->has_token ? u->c->token.token : NULL, u->c->req_id,
                    &result, out, outcap, &off) != 0)
    {
        /* bizd 不可达：回带结果码的响应，避免客户端空等 */
        off = 0u;
        QLOG_WARN(u->g->log, "转发业务请求失败 %s cmd=%u",
                  netd_cid(u->c, cid, sizeof(cid)), (unsigned)cmd);
        netd_put_result(out, outcap, &off, Q_IPC_R_UNAVAILABLE);
        *out_len = off;
        return 0;
    }

    /*
     * 业务层失败（如 BADCRED / UNAUTHORIZED / 下游返回非 0）**不**升级为断连：
     * ipc_forward 已把下游 RESULT 装进 out，此处原样回给客户端即可；
     * 若返回非 0，netd_srv 会 break 断连，客户端只能看到 -1 而拿不到真实结果码，
     * 登录失败、Token 过期这些「可恢复」场景将全部退化成连接断开。
     */
    if (result != Q_IPC_R_OK)
        QLOG_WARN(u->g->log, "业务请求 result=%u %s cmd=%u", (unsigned)result,
                  netd_cid(u->c, cid, sizeof(cid)), (unsigned)cmd);

    *out_len = off;
    return 0;
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
    /* SALT_QUERY：登录前拉取账号 salt（方案A），转发 bizd 即可（与业务转发同构） */
    if (q_chan_disp_register(d, Q_CHAN_CMD_SALT_QUERY,  hnd_biz,       ud) != 0)
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
    char     cid[INET6_ADDRSTRLEN + 128];

    login_bind_ids(c, body, blen);

    QLOG_INFO(c->g->log, "转发登录请求 %s -> bizd",
              netd_cid(c, cid, sizeof(cid)));

    /* LOGIN 阶段尚无 Token：显式传 NULL（c->token.token 此刻还是全零） */
    if (ipc_forward(c->g, &c->src_addr, Q_CHAN_CMD_LOGIN, body, blen,
                    c->channel_id, c->token.user_id, NULL, c->req_id,
                    &result, out, outcap, out_len) != 0)
    {
        /* bizd 不可用：仍回带结果码的响应，避免客户端空等 */
        size_t off = 0u;

        QLOG_WARN(c->g->log, "转发登录请求失败 %s",
                  netd_cid(c, cid, sizeof(cid)));
        netd_put_result(out, outcap, &off, Q_IPC_R_UNAVAILABLE);
        *out_len = off;
        return 0;
    }

    QLOG_INFO(c->g->log, "登录请求已转发 %s result=%u",
              netd_cid(c, cid, sizeof(cid)), result);

    if (result != Q_IPC_R_OK)
        return 0;

    /* 签发会话 Token，绑定 channel_id + user_id + device_id + 源地址（v4/v6 统一，
       扩为 [1B 族][16B 地址] 以支持 IPv6 客户端溯源，与此处 netd_addr_t 一致）。 */
    {
        uint8_t tfam = (c->src_addr.fam == AF_INET6) ? 6u : 4u;
        if (q_chan_token_issue(c->channel_id, c->token.user_id, c->token.device_id,
                               tfam, c->src_addr.b, NETD_TOKEN_TTL_SEC, &c->token) != 0)
            return 0;
    }

    c->has_token = 1;
    /*
     * Token 回给客户端必须用 Q_IPC_T_AUTHTOK(0x0106)：
     *   0x0106 = 客户端侧契约（qc_login / 业务请求携带 Token 均用此 tag，
     *            见《客户端通信接口与开发指南》与 include_ipc_proto.h 注释）；
     *   0x0006 = netd <-> bizd 的内部 wire tag（ipc_forward 转发用，不下发客户端）。
     * 历史上此处误用 0x0006，导致 qc_login 取不到 Token、任何一端都登不进去。
     */
    {
        size_t n = 0u;
        if (q_codec_tlv_encode(out + *out_len, outcap - *out_len,
                               Q_IPC_T_AUTHTOK, c->token.token,
                               Q_CHAN_TOKEN_LEN, &n) == 0)
            *out_len += n;
    }
    return 1;
}
