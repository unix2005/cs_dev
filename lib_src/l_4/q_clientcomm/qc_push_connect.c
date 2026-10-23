/**
 * @file qc_push_connect.c
 * @brief M5：qc_push_connect —— 建立整条推送连接（knock->connect->hello->salt->login->BEGIN->重放）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 初始启动与断线重连走**同一个函数**：区别只在 pwd_hash 的来源
 * （启动 = 调用方瞬态口令本地派生；重连 = on_relogin 向 UI 索取）。
 * 复用同一路径的意义：重连走的代码就是启动时验证过的代码，不会出现
 * "启动能连上、重连连不上"的第二条从未跑过的路径。
 *
 * 口令红线：pwd_hash 只在栈上存活到 qc_login 返回，随后立即清零。
 */
#include "qc_push_int.h"   /* 公开 API + 内部结构体 */
#include "qc_push_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include_ipc_proto.h"   /* Q_PBKDF2_ITER_DEFAULT / Q_IPC_R_* */
#include "q_mem.h"

/* 解析 "host:port" -> host/port。成功 0，失败 -1（与 qc_keeper_rebuild 同规则） */
static int split_ep(const char *s, char *host, size_t cap, int *port)
{
    const char *colon;
    size_t      n;

    if (!s || !host || !port || cap == 0u)
        return -1;
    colon = strrchr(s, ':');
    if (!colon || colon == s)
        return -1;
    n = (size_t)(colon - s);
    if (n >= cap)
        return -1;
    memcpy(host, s, n);
    host[n] = '\0';
    *port = atoi(colon + 1);
    return (*port > 0) ? 0 : -1;
}

/* 发送 PUSH_BEGIN 并等待服务端确认（RESULT=0）。
   这是建连阶段唯一一次 lockstep 请求——必须在接收线程启动前完成，
   否则两个线程同时收包会破坏"一问一答"。 */
static int send_begin(qc_push_t *p)
{
    uint8_t resp[256];
    size_t  rl = 0;
    uint16_t cmd = 0;
    size_t  off = 0;
    int     result = -1;

    if (qc_request(p->sess, Q_CHAN_CMD_PUSH_BEGIN, (const uint8_t *)"", 0,
                   resp, sizeof(resp), &rl, &cmd) != 0)
        return -1;

    while (off < rl)
    {
        uint16_t tag;
        const uint8_t *v;
        uint16_t vlen;
        size_t used;
        if (q_codec_tlv_decode(resp + off, rl - off, &tag, &v, &vlen, &used) != 0)
            break;
        off += used;
        if (tag == Q_IPC_T_RESULT && vlen >= 4u)
            result = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                     ((uint32_t)v[2] << 8) | (uint32_t)v[3];
    }
    q_mem_zero(resp, sizeof(resp));
    return (result == 0) ? 0 : (result > 0 ? result : -1);
}

/* 重放当前完整订阅集合（分片 ADD）。仅在（重）连成功后调用 ——
   服务端会话是新建的，不知道客户端之前订过什么。 */
static int replay_subs(qc_push_t *p)
{
    uint32_t i;

    qc_mutex_lock(&p->lock);
    for (i = 0; i < p->nvid; i += Q_PUSH_SHARD_MAX)
    {
        uint8_t body[Q_PUSH_SHARD_MAX * 48u + 64u];
        size_t  blen = 0;
        uint32_t cnt = (p->nvid - i < Q_PUSH_SHARD_MAX) ? (p->nvid - i)
                                                        : Q_PUSH_SHARD_MAX;
        const char *ids[Q_PUSH_SHARD_MAX];
        uint32_t k;

        for (k = 0; k < cnt; ++k)
            ids[k] = p->vids[i + k];
        if (qc_push_build_delta(body, sizeof(body), Q_PUSH_OP_ADD,
                                ids, cnt, &blen) != 0 ||
            q_chan_transmit(p->sess->chan, p->sess->fd, body, blen,
                            Q_CHAN_CMD_PUSH_SUB_DELTA) != 0)
        {
            q_mem_zero(body, sizeof(body));
            qc_mutex_unlock(&p->lock);
            return -1;
        }
        q_mem_zero(body, sizeof(body));
    }
    qc_mutex_unlock(&p->lock);
    return 0;
}

int qc_push_connect(qc_push_t *p, const char *password)
{
    uint8_t  salt[64];
    size_t   slen = 0u;
    uint8_t  pwd_hash[32];
    uint32_t iter;
    int      rc;

    if (!p || !p->cfg.host[0] || p->cfg.port == 0u)
        return -1;

    /* ---- 0) 旧连接旧通道全部作废重来（M4 教训：复用已握手 chan 必失败） ---- */
    if (p->sess)
    {
        qc_session_destroy(p->sess);
        p->sess = NULL;
    }
    p->sess = qc_session_create(p->cfg.pub_der, p->cfg.pub_len,
                                p->cfg.seq0, p->cfg.key_id);
    if (!p->sess)
        return -1;

    /* ---- 1) SPA 敲门（推送栈独立 knockd；目标端口 = 推送端口） ---- */
    if (p->cfg.knock[0] && p->cfg.spa_key_len > 0u)
    {
        char    khost[64];
        int     kport = 0;
        uint8_t uid[32];
        size_t  ulen;

        if (split_ep(p->cfg.knock, khost, sizeof(khost), &kport) == 0)
        {
            memset(uid, 0, sizeof(uid));
            ulen = strlen(p->cfg.user);
            if (ulen > sizeof(uid))
                ulen = sizeof(uid);
            memcpy(uid, p->cfg.user, ulen);

            /* 敲门失败不致命：继续尝试连接（可能是 knockd 未部署而端口本来就通） */
            (void)qc_knock(khost, kport, p->cfg.spa_key, p->cfg.spa_key_len,
                           p->cfg.spa_key_id, uid, p->cfg.port, 300);
        }
    }

    /* ---- 2) TCP 连接 + SM2 握手（域名由 q_net_connect 内部分辨） ---- */
    if (qc_connect(p->sess, p->cfg.host, (int)p->cfg.port) != 0)
        return -1;

    /* ---- 3) HELLO ---- */
    rc = qc_hello(p->sess);
    if (rc != 0)
        return (rc > 0) ? rc : -1;

    /* ---- 4) SALT_QUERY（方案A：服务端按账号下发盐） ---- */
    rc = qc_fetch_salt(p->sess, p->cfg.user, salt, sizeof(salt), &slen);
    if (rc != 0 || slen == 0u)
    {
        q_mem_zero(salt, sizeof(salt));
        return (rc > 0) ? rc : -1;
    }

    /* ---- 5) pwd_hash：启动路径本地派生；重连路径向 UI 索取 ---- */
    iter = p->cfg.pbkdf2_iter ? p->cfg.pbkdf2_iter : (uint32_t)Q_PBKDF2_ITER_DEFAULT;
    memset(pwd_hash, 0, sizeof(pwd_hash));
    if (password)
    {
        if (qc_pbkdf2_sm3((const uint8_t *)password, strlen(password),
                          salt, slen, iter, pwd_hash) != 0)
        {
            q_mem_zero(salt, sizeof(salt));
            return -1;
        }
    }
    else
    {
        if (!p->on_relogin ||
            p->on_relogin(p->relogin_ud, salt, slen, iter, pwd_hash) != 0)
        {
            q_mem_zero(salt, sizeof(salt));
            return -2;                       /* UI 无法提供口令：本轮放弃 */
        }
    }
    q_mem_zero(salt, sizeof(salt));

    /* ---- 6) LOGIN（推送连接自己的 Token，绑定本通道 channel_id） ---- */
    rc = qc_login(p->sess, p->cfg.user, pwd_hash, p->cfg.otp,
                  p->cfg.devid, p->cfg.devid_len,
                  p->cfg.cver[0] ? p->cfg.cver : "qc_push/1.2",
                  p->sess->token);
    q_mem_zero(pwd_hash, sizeof(pwd_hash));
    if (rc != 0)
        return (rc > 0) ? rc : -1;

    /* ---- 7) PUSH_BEGIN：通知 netd/push 服务建立推送会话 ---- */
    rc = send_begin(p);
    if (rc != 0)
        return (rc > 0) ? rc : -1;

    /* ---- 8) 重放订阅集合（服务端会话是新的，必须全量补发） ---- */
    if (replay_subs(p) != 0)
        return -1;

    p->connected = 1;
    p->attempts  = 0;
    return 0;
}
