/**
 * @file push_handler.c
 * @brief 推送微服务帧处理：BEGIN（建立推送会话）/ SUB_DELTA（增量订阅）
 *
 * 与 biz_handler.c 的分工差异：biz 是"一问一答"（每帧必有响应），pushd 是
 * **混合语义** —— BEGIN 有响应（lockstep，客户端要拿 RESULT 才知道会话建成），
 * SUB_DELTA **无响应**（fire-and-forget：TCP 有序 + 服务端本地集合合并即可，
 * 回执只会把"订阅"这种允许最终一致的操作变成同步瓶颈）。
 *
 * 调用约定：push_conn_worker 持 g_lock 调用本函数（BEGIN 要回写 socket）。
 */
#include "srv_push.h"

#include <stdlib.h>
#include <string.h>

/* 日志中单条消息展示的最大长度（超过则截断，避免超长日志行刷屏） */
#define PUSH_LOG_CAP 2048u

/* token 是否有效（桩）：存在且非全零。全零意味着 netd_B 没把会话 Token 透传下来，
   或客户端根本没登录 —— 两者都必须拒绝，否则鉴权形同虚设。 */
static int token_ok(const uint8_t *token, uint16_t len)
{
    uint16_t i;
    int      nz = 0;

    if (!token || len < PUSH_TOKEN_LEN)
        return 0;
    for (i = 0; i < PUSH_TOKEN_LEN; ++i)
        if (token[i]) { nz = 1; break; }
    return nz;
}

/* 回一帧 RESULT（BEGIN 应答专用） */
static int send_result(push_session_t *s, uint32_t result)
{
    uint8_t resp[16];
    size_t  off = 0u, n = 0u;
    uint8_t rb[4];

    push_put_be32(rb, result);
    if (q_codec_tlv_encode(resp, sizeof(resp), Q_IPC_T_RESULT, rb, 4, &n) != 0)
        return -1;
    off = n;
    return push_send_frame(s->fd, resp, off);
}

/* BEGIN：登记会话（组织/账号/通道绑定），应答 RESULT=0。
   返回 0 会话建立（或已建立）；<0 连接应关闭。 */
static int handle_begin(push_session_t *s, const uint8_t *tok, uint16_t toklen,
                        const uint8_t *chanid, const uint8_t *userid,
                        const char *user_name)
{
    if (g_pcfg.require_token && !token_ok(tok, toklen))
    {
        QLOG_WARN(g_push.log, "push BEGIN 被拒：token 无效（user=%s）",
                  user_name ? user_name : "?");
        (void)send_result(s, Q_IPC_R_UNAUTHORIZED);
        return 0;                   /* 应答后由客户端自行关闭 */
    }

    if (s->active)
    {
        /* 重复 BEGIN：幂等应答（客户端重连时会先停后起，正常不会到这里；
           防御性处理，避免一次重发就把好会话打成错误态） */
        return send_result(s, Q_IPC_R_OK) == 0 ? 0 : -1;
    }

    snprintf(s->user_name, sizeof(s->user_name), "%s", user_name ? user_name : "");
    snprintf(s->org_id, sizeof(s->org_id), "%s", push_user_org(s->user_name));
    if (userid)
        memcpy(s->user_id, userid, 32u);        /* userid 固定 32B（SM3(用户名)） */
    if (chanid)
        memcpy(s->chan_id, chanid, 32u);

    if (push_session_add(s) != 0)
    {
        QLOG_WARN(g_push.log, "push BEGIN 失败：会话表已满（user=%s）", s->user_name);
        (void)send_result(s, Q_IPC_R_UNAVAILABLE);
        return 0;
    }
    s->active = 1;

    QLOG_INFO(g_push.log, "push 会话建立（user=%s org=%s）", s->user_name, s->org_id);
    return send_result(s, Q_IPC_R_OK) == 0 ? 0 : -1;
}

/* SUB_DELTA：解析 BODY 内的推送域 TLV（OP + VID*n），应用到本地订阅集合 */
static void handle_sub_delta(push_session_t *s, const uint8_t *body, size_t blen)
{
    size_t   off = 0u;
    int      op = -1;
    uint32_t nadd = 0u, nrm = 0u;

    while (off < blen)
    {
        uint16_t       tag, vlen;
        const uint8_t *val;
        size_t         used;

        if (q_codec_tlv_decode(body + off, blen - off, &tag, &val, &vlen, &used) != 0)
            break;
        off += used;

        if (tag == Q_IPC_T_PUSH_OP && vlen >= 1u)
        {
            op = (int)val[0];
        }
        else if (tag == Q_IPC_T_PUSH_VID && vlen > 0u)
        {
            char vid[PUSH_ID_MAX];

            if (vlen >= sizeof(vid))
                continue;                    /* 超长键：不收，防静默截断订到别的车 */
            memcpy(vid, val, vlen);
            vid[vlen] = '\0';
            if (op == (int)Q_PUSH_OP_ADD)
            {
                if (push_sub_add(s, vid) == 0) ++nadd;
            }
            else if (op == (int)Q_PUSH_OP_REMOVE)
            {
                push_sub_remove(s, vid);
                ++nrm;
            }
            /* op 尚未出现（客户端总把 OP 放在最前，防御性：忽略提前出现的 VID） */
        }
    }

    if (op != (int)Q_PUSH_OP_ADD && op != (int)Q_PUSH_OP_REMOVE)
    {
        QLOG_WARN(g_push.log, "push SUB_DELTA 缺 OP 或 OP 非法，整帧忽略（user=%s）",
                  s->user_name);
        return;
    }
    QLOG_INFO(g_push.log, "push 订阅更新（user=%s op=%s n=%u 集合=%u）",
              s->user_name, op == (int)Q_PUSH_OP_ADD ? "ADD" : "REMOVE",
              op == (int)Q_PUSH_OP_ADD ? nadd : nrm, (unsigned)s->nvid);
}

int push_handle_frame(push_session_t *s, const uint8_t *frame, size_t flen)
{
    size_t         off = 0u;
    uint16_t       cmd = 0u;
    const uint8_t *tok = NULL, *chanid = NULL, *userid = NULL, *body = NULL;
    char           user_name[PUSH_USER_MAX];
    uint16_t       toklen = 0u, bodylen = 0u;

    if (!s || !frame || flen < 4u)
        return -1;

    user_name[0] = '\0';       /* 无 USERNAME TLV 时保持空串（未知用户 -> 根组织） */

    while (off < flen)
    {
        uint16_t       tag, vlen;
        const uint8_t *val;
        size_t         used;

        if (q_codec_tlv_decode(frame + off, flen - off, &tag, &val, &vlen, &used) != 0)
            return -1;
        off += used;

        switch (tag)
        {
        case Q_IPC_T_CMD:
            if (vlen >= 2u)
                cmd = (uint16_t)(((uint16_t)val[0] << 8) | val[1]);
            break;
        case Q_IPC_T_TOKEN:
            tok = val; toklen = vlen;
            break;
        case Q_IPC_T_CHANID:
            if (vlen >= 32u) chanid = val;
            break;
        case Q_IPC_T_USERID:
            if (vlen >= 32u) userid = val;
            break;
        case Q_IPC_T_USERNAME:
            if (vlen > 0u && vlen < sizeof(user_name))
            {
                memcpy(user_name, val, vlen);
                user_name[vlen] = '\0';
            }
            break;
        case Q_IPC_T_BODY:
            body = val; bodylen = vlen;
            break;
        default:
            break;                          /* SRCIP 等只用于对账，忽略 */
        }
    }

    if (cmd == Q_CHAN_CMD_PUSH_BEGIN)
        return handle_begin(s, tok, toklen, chanid, userid, user_name[0] ? user_name : NULL);

    if (!s->active)
    {
        /* 未 BEGIN 就发业务帧：协议违规，直接断（netd_B 侧同语义） */
        QLOG_WARN(g_push.log, "push 未 BEGIN 收到 cmd=%u，断连", (unsigned)cmd);
        return -1;
    }

    if (cmd == Q_CHAN_CMD_PUSH_SUB_DELTA)
    {
        if (body && bodylen > 0u)
            handle_sub_delta(s, body, bodylen);
        return 0;                           /* 免回执 */
    }

    /* 推送连接上不应出现其它 cmd：忽略但不断连（与客户端接收线程同策略） */
    QLOG_DEBUG(g_push.log, "push 忽略未知 cmd=%u（user=%s）", (unsigned)cmd, s->user_name);
    return 0;
}
