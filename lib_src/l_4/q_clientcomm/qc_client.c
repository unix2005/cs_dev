/**
 * @file qc_client.c
 * @brief l_4 客户端通信内核（libqclientcomm）实现
 * @layer  lib_src/l_4/q_clientcomm  ->  产出 libqclientcomm(.a/.so/.dll)
 * @note   纯 C，跨三端（WinUI3 / GTK4 / macOS 原生）共享；对外仅暴露 qc_client.h 的 C ABI 门面。
 *         内部封装 q_chan(SM2 ECDH + SM4-GCM 安全通道) / q_net(阻塞收发) /
 *         q_codec(TLV) / q_crypto(PBKDF2-SM3) / q_mem(敏感清零)。
 *         UI 层绝不直接碰 q_fd_t / q_chan_t。
 */
#include "qc_client.h"

#include <stdlib.h>
#include <string.h>

#include "q_chan.h"            /* q_chan_t / q_chan_create / handshake / transmit / receive / destroy */
#include "q_net.h"             /* q_net_connect / q_net_close / UDP 收发 / 地址解析 */
#include "q_codec.h"           /* q_codec_tlv_encode / q_codec_tlv_decode */
#include "q_crypto.h"          /* q_crypto_pbkdf2_sm3 */
#include "q_mem.h"             /* q_mem_zero */
#include "q_spa.h"             /* q_spa_build（SPA 敲门） */
#include "include_ipc_proto.h" /* Q_IPC_T_* 内层 TLV tag / Q_IPC_R_* 结果码 / Q_PBKDF2_* */

/* struct timeval（setsockopt SO_RCVTIMEO/SO_SNDTIMEO 需要）；Windows 由 winsock2.h 提供 */
#ifndef Q_SYS_WINDOWS
#include <sys/time.h>
#endif

struct qc_session
{
    q_chan_t *chan;                  /* 安全通道句柄（内部） */
    q_fd_t    fd;                    /* 已连接 socket（内部；(q_fd_t)-1 表示未连接） */
    uint8_t   token[Q_CHAN_TOKEN_LEN]; /* 登录后缓存的会话 Token（32B） */
    int       authed;                /* 是否已登录 */
};

/* 设置阻塞 socket 的收发超时，避免异常对端令收包线程永久阻塞。
   平台差异：Windows 取 DWORD 毫秒，POSIX 取 struct timeval。 */
static int qc_set_timeout(q_fd_t fd, int sec)
{
#ifdef Q_SYS_WINDOWS
    DWORD ms = (DWORD)((sec > 0 ? sec : 0) * 1000);
    if (setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms)) != 0)
        return -1;
    return setsockopt((SOCKET)fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec  = (time_t)(sec > 0 ? sec : 0);
    tv.tv_usec = 0;
    if (setsockopt((int)fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
        return -1;
    return setsockopt((int)fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/* 业务请求体内层 TLV 拼装缓冲上限（AUTHTOK+BIZ_TYPE+BIZ_PARAM+分页）。
   BIZ_PARAM 走文本 JSON，2048 对过滤条件/分页场景足够；超出直接报错而非静默截断。 */
#define QC_BIZ_BODY_MAX 2048u

/* 从明文 TLV 中提取 RESULT（uint32 网络序）。
   返回 >=0 找到的 RESULT 值（0=成功）；-1 未找到 RESULT（视为无错误指示）；
   -2 TLV 解析错误。 */
static int qc_extract_result(const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off + 4 <= len)
    {
        uint16_t tag;
        const uint8_t *v;
        uint16_t vlen;
        size_t used;
        if (q_codec_tlv_decode(buf + off, len - off, &tag, &v, &vlen, &used) != 0)
            return -2;
        if (tag == Q_IPC_T_RESULT && vlen >= 4)
        {
            return ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                   ((uint32_t)v[2] << 8) | (uint32_t)v[3];
        }
        off += used;
    }
    return -1;
}

/* 把 RESULT 映射为返回值：0 成功；>0 结果码；-1 断连/解析失败。 */
static int qc_result_to_ret(int r)
{
    if (r == -2) return -1;
    if (r == -1) return 0;
    return r; /* 0 或正结果码 */
}

qc_session_t *qc_session_create(const uint8_t *server_pub_der, size_t server_pub_len,
                                uint64_t seq0, uint16_t key_id)
{
    qc_session_t *s;

    if (!server_pub_der || server_pub_len == 0)
        return NULL;

    s = (qc_session_t *)calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    s->fd = (q_fd_t)-1;
    s->authed = 0;

    /* 客户端：本地密钥对传 NULL（握手时生成临时密钥）；仅加载服务端静态公钥用于验签身份。 */
    s->chan = q_chan_create(Q_CHAN_MODE_CLIENT, NULL, 0, NULL, 0,
                            server_pub_der, server_pub_len, seq0, key_id);
    if (!s->chan)
    {
        free(s);
        return NULL;
    }
    return s;
}

void qc_session_destroy(qc_session_t *s)
{
    if (!s)
        return;
    qc_close(s);
    if (s->chan)
        q_chan_destroy(s->chan);
    q_mem_zero(s->token, sizeof(s->token));
    free(s);
}

void qc_close(qc_session_t *s)
{
    if (!s)
        return;
    if (s->fd != (q_fd_t)-1)
    {
        q_net_close(s->fd);
        s->fd = (q_fd_t)-1;
    }
    s->authed = 0;
}

/*
 * SPA 敲门：UDP 单包，无响应（fire-and-forget）。
 * ip_hint 填 0 —— knockd 以 recvfrom 的真实源 IP 为准，防 IP 欺骗。
 */
int qc_knock(const char *knock_host, int knock_port,
             const uint8_t *spa_key, size_t key_len, uint8_t key_id,
             const uint8_t user_id[32], uint16_t req_port, uint16_t req_ttl)
{
    q_spa_pkt_t          pkt;
    q_fd_t               fd = (q_fd_t)-1;
    struct sockaddr_storage ss;
    socklen_t            slen = 0;
    uint8_t              ip_hint[16];
    ssize_t              n;

    if (!knock_host || !spa_key || key_len == 0u || !user_id)
        return -1;

    memset(&pkt, 0, sizeof(pkt));
    memset(ip_hint, 0, sizeof(ip_hint));
    if (q_spa_build(spa_key, key_len, key_id, user_id, req_port, req_ttl, ip_hint, &pkt) != 0)
        return -1;

    if (q_net_socket_udp(&fd) != 0)
        return -1;
    if (q_net_addr_resolve(knock_host, knock_port, &ss, &slen) != 0)
    {
        q_net_close(fd);
        return -1;
    }

    n = q_net_udp_send(fd, &pkt, sizeof(pkt), (const struct sockaddr *)&ss, slen);
    q_net_close(fd);

    q_mem_zero(&pkt, sizeof(pkt));   /* 报文中含密文与 tag，用完即清 */
    return (n == (ssize_t)sizeof(pkt)) ? 0 : -1;
}

int qc_connect(qc_session_t *s, const char *host, int port)
{
    if (!s || !s->chan || !host)
        return -1;
    if (q_net_connect(host, port, &s->fd) != 0)
        return -1;
    /* 阻塞 socket 设收发超时（30s），避免异常对端令线程永久阻塞 */
    qc_set_timeout(s->fd, 30);
    if (q_chan_handshake(s->chan, s->fd) != 0)
    {
        q_net_close(s->fd);
        s->fd = (q_fd_t)-1;
        return -1;
    }
    return 0;
}

/* 追加一条 TLV 到 out（cap 为剩余容量），更新 *off */
static int qc_append_tlv(uint8_t *out, size_t cap, size_t *off,
                         uint16_t tag, const uint8_t *val, uint16_t vlen)
{
    size_t n = 0;
    if (q_codec_tlv_encode(out + *off, cap - *off, tag, val, vlen, &n) != 0)
        return -1;
    *off += n;
    return 0;
}

/* 构造 LOGIN 业务体：USERNAME + PWDHASH(32) + OTP + DEVID + CVER */
static int qc_build_login_body(const char *username, const uint8_t pwd_hash[32],
                               const char *otp, const uint8_t *devid, size_t devid_len,
                               const char *cver, uint8_t *body, size_t cap, size_t *blen)
{
    size_t off = 0;
    if (qc_append_tlv(body, cap, &off, Q_IPC_T_USERNAME,
                      (const uint8_t *)username, (uint16_t)strlen(username)) != 0)
        return -1;
    if (qc_append_tlv(body, cap, &off, Q_IPC_T_PWDHASH, pwd_hash, 32) != 0)
        return -1;
    if (qc_append_tlv(body, cap, &off, Q_IPC_T_OTP,
                      (const uint8_t *)otp, (uint16_t)strlen(otp)) != 0)
        return -1;
    if (qc_append_tlv(body, cap, &off, Q_IPC_T_DEVID, devid, (uint16_t)devid_len) != 0)
        return -1;
    if (qc_append_tlv(body, cap, &off, Q_IPC_T_CVER,
                      (const uint8_t *)cver, (uint16_t)strlen(cver)) != 0)
        return -1;
    *blen = off;
    return 0;
}

int qc_request(qc_session_t *s, uint16_t cmd,
               const uint8_t *body, size_t body_len,
               uint8_t *resp_out, size_t resp_cap, size_t *resp_len_out, uint16_t *cmd_out)
{
    if (!s || !s->chan || s->fd == (q_fd_t)-1)
        return -1;
    if (body_len > 0 && !body)
        return -1;
    /* 严格 lockstep：先加密发送，再阻塞接收，成对完成。 */
    if (q_chan_transmit(s->chan, s->fd, body, body_len, cmd) != 0)
        return -1;
    if (q_chan_receive(s->chan, s->fd, resp_out, resp_cap, resp_len_out, cmd_out) != 0)
        return -1;
    return 0;
}

/*
 * 业务查询（BIZ_QUERY）便捷封装：三端共用，杜绝各端手拼 TLV 造成的协议漂移。
 * 请求体：AUTHTOK(32B) + BIZ_TYPE(文本) + [BIZ_PARAM(文本)] + [OFFSET/LIMIT]
 * 响应：原样写入 resp_out（RESULT + BODY），本门面不解析 JSON。
 */
int qc_biz_query(qc_session_t *s, const char *biz_type, const char *param_json,
                 uint32_t offset, uint32_t limit,
                 uint8_t *resp_out, size_t resp_cap, size_t *resp_len_out)
{
    uint8_t body[QC_BIZ_BODY_MAX];
    size_t  blen = 0u, plen;
    uint16_t cmd_out = 0;

    if (!s || !biz_type || !biz_type[0] || !resp_out || resp_cap == 0u || !resp_len_out)
        return -1;
    if (!s->authed)
        return -1;                      /* 未登录：没有可随带的 Token */

    plen = param_json ? strlen(param_json) : 0u;
    if (plen > (size_t)(QC_BIZ_BODY_MAX - 256u))
        return -1;                      /* 参数过大：宁可报错也不静默截断 */

    /* 1) 会话 Token（业务鉴权凭据；服务端校验非空且绑定本通道） */
    if (qc_append_tlv(body, sizeof(body), &blen, Q_IPC_T_AUTHTOK,
                      s->token, Q_CHAN_TOKEN_LEN) != 0)
        return -1;

    /* 2) 业务子类型 */
    if (qc_append_tlv(body, sizeof(body), &blen, Q_IPC_T_BIZ_TYPE,
                      (const uint8_t *)biz_type, (uint16_t)strlen(biz_type)) != 0)
        return -1;

    /* 3) 业务参数（可选） */
    if (plen > 0u &&
        qc_append_tlv(body, sizeof(body), &blen, Q_IPC_T_BIZ_PARAM,
                      (const uint8_t *)param_json, (uint16_t)plen) != 0)
        return -1;

    /* 4) 分页（limit==0 表示不分页） */
    if (limit > 0u)
    {
        uint8_t u32[4];

        u32[0] = (uint8_t)(offset >> 24); u32[1] = (uint8_t)(offset >> 16);
        u32[2] = (uint8_t)(offset >> 8);  u32[3] = (uint8_t)(offset);
        if (qc_append_tlv(body, sizeof(body), &blen, Q_IPC_T_PAGE_OFFSET, u32, 4) != 0)
            return -1;

        u32[0] = (uint8_t)(limit >> 24); u32[1] = (uint8_t)(limit >> 16);
        u32[2] = (uint8_t)(limit >> 8);  u32[3] = (uint8_t)(limit);
        if (qc_append_tlv(body, sizeof(body), &blen, Q_IPC_T_PAGE_LIMIT, u32, 4) != 0)
            return -1;
    }

    if (qc_request(s, Q_CHAN_CMD_BIZ_QUERY, body, blen,
                   resp_out, resp_cap, resp_len_out, &cmd_out) != 0)
        return -1;

    return qc_result_to_ret(qc_extract_result(resp_out, *resp_len_out));
}

int qc_hello(qc_session_t *s)
{
    uint8_t resp[256];
    size_t  rl = 0;
    uint16_t cmd = 0;
    if (qc_request(s, Q_CHAN_CMD_HELLO, (const uint8_t *)"", 0, resp, sizeof(resp), &rl, &cmd) != 0)
        return -1;
    return qc_result_to_ret(qc_extract_result(resp, rl));
}

/*
 * 登录前拉取该账号 salt（方案A：服务端按账号下发的随机盐，防彩虹表）。
 * 请求体：TLV(USERNAME)；响应体：TLV(RESULT) + TLV(BODY=原始 salt 字节)。
 * salt 经 Q_IPC_T_BODY 透传（客户端 TLV-only，不解析 JSON）。
 */
int qc_fetch_salt(qc_session_t *s, const char *username,
                  uint8_t *salt_out, size_t salt_cap, size_t *salt_len_out)
{
    uint8_t body[256];
    uint8_t resp[512];
    size_t  blen = 0, rl = 0;
    uint16_t cmd = 0;
    size_t  off = 0;

    if (!s || !username || !salt_out || salt_cap == 0 || !salt_len_out)
        return -1;
    if (qc_append_tlv(body, sizeof(body), &blen, Q_IPC_T_USERNAME,
                      (const uint8_t *)username, (uint16_t)strlen(username)) != 0)
        return -1;

    if (qc_request(s, Q_CHAN_CMD_SALT_QUERY, body, blen, resp, sizeof(resp), &rl, &cmd) != 0)
        return -1;

    {
        int result = -1;
        int have_salt = 0;
        while (off < rl)
        {
            uint16_t tag;
            const uint8_t *v;
            uint16_t vlen;
            size_t used;
            if (q_codec_tlv_decode(resp + off, rl - off, &tag, &v, &vlen, &used) != 0)
                break;
            off += used;
            if (tag == Q_IPC_T_RESULT && vlen >= 4)
            {
                result = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                         ((uint32_t)v[2] << 8) | (uint32_t)v[3];
            }
            else if (tag == Q_IPC_T_BODY && vlen > 0 && vlen <= salt_cap)
            {
                memcpy(salt_out, v, vlen);
                *salt_len_out = vlen;
                have_salt = 1;
            }
        }
        q_mem_zero(resp, sizeof(resp));
        if (result == 0 && have_salt)
            return 0;
        return (result > 0) ? result : -1;
    }
}

int qc_login(qc_session_t *s, const char *username, const uint8_t pwd_hash[32],
             const char *otp, const uint8_t *devid, size_t devid_len,
             const char *cver, uint8_t token_out[32])
{
    uint8_t body[512];
    uint8_t resp[512];
    size_t  blen = 0, rl = 0;
    uint16_t cmd = 0;
    size_t  off = 0;

    if (!s || !username || !pwd_hash || !otp || !devid || devid_len == 0 || !cver || !token_out)
        return -1;
    if (qc_build_login_body(username, pwd_hash, otp, devid, devid_len, cver,
                            body, sizeof(body), &blen) != 0)
    {
        q_mem_zero(body, sizeof(body));
        return -1;
    }

    if (qc_request(s, Q_CHAN_CMD_LOGIN, body, blen, resp, sizeof(resp), &rl, &cmd) != 0)
    {
        q_mem_zero(body, sizeof(body));
        return -1;
    }
    q_mem_zero(body, sizeof(body)); /* 口令派生值不滞留 */

    /* 解析响应：RESULT + AUTHTOK(32B) */
    {
        int result = -1;
        int have_tok = 0;
        while (off < rl)
        {
            uint16_t tag;
            const uint8_t *v;
            uint16_t vlen;
            size_t used;
            if (q_codec_tlv_decode(resp + off, rl - off, &tag, &v, &vlen, &used) != 0)
                break;
            off += used;
            if (tag == Q_IPC_T_RESULT && vlen >= 4)
            {
                result = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                         ((uint32_t)v[2] << 8) | (uint32_t)v[3];
            }
            else if ((tag == Q_IPC_T_AUTHTOK || tag == Q_IPC_T_TOKEN) && vlen >= 32 && !have_tok)
            {
                /*
                 * 首选 Q_IPC_T_AUTHTOK(0x0106) —— 客户端侧契约（与业务请求携带 Token 的
                 * tag 一致，见《客户端通信接口与开发指南》）。
                 * 同时接受 Q_IPC_T_TOKEN(0x0006)：历史 netd 版本用它回 Token（那是
                 * netd<->bizd 的 wire tag），为兼容存量服务端（三端灰度升级期间）保留。
                 */
                memcpy(s->token, v, 32);
                have_tok = 1;
            }
        }
        q_mem_zero(resp, sizeof(resp));
        if (result == 0 && have_tok)
        {
            memcpy(token_out, s->token, 32);
            s->authed = 1;
            return 0;
        }
        return (result > 0) ? result : -1;
    }
}

int qc_heartbeat(qc_session_t *s)
{
    uint8_t resp[256];
    size_t  rl = 0;
    uint16_t cmd = 0;
    if (qc_request(s, Q_CHAN_CMD_HEARTBEAT, (const uint8_t *)"", 0, resp, sizeof(resp), &rl, &cmd) != 0)
        return -1;
    return qc_result_to_ret(qc_extract_result(resp, rl));
}

int qc_logout(qc_session_t *s)
{
    uint8_t resp[256];
    size_t  rl = 0;
    uint16_t cmd = 0;
    if (qc_request(s, Q_CHAN_CMD_LOGOUT, (const uint8_t *)"", 0, resp, sizeof(resp), &rl, &cmd) != 0)
        return -1;
    s->authed = 0;
    q_mem_zero(s->token, sizeof(s->token));
    return qc_result_to_ret(qc_extract_result(resp, rl));
}

int qc_pbkdf2_sm3(const uint8_t *pass, size_t plen, const uint8_t *salt, size_t slen,
                  uint32_t iter, uint8_t out[32])
{
    if (!pass || plen == 0 || !salt || slen == 0 || !out)
        return -1;
    /* 仅本地运算，不触网；outlen 固定 32 */
    return q_crypto_pbkdf2_sm3(pass, plen, salt, slen, iter, out, 32);
}
