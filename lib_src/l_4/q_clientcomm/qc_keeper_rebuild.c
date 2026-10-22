/**
 * @file qc_keeper_rebuild.c
 * @brief M4 会话治理：断线后重建整条链路（close -> [SPA敲门] -> connect -> hello -> 重登）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 用于连接真正不可用的场景（TCP 断、握手通道作废、服务端重启）。
 * 与 qc_keeper_reauth() 的区别：reauth 只在**原连接**上重发登录；rebuild 会拆掉
 * 重来一次完整流程 —— 开销大，但能恢复"连接已废"这类 reauth 救不了的情况。
 *
 * 敲门（knockd）在每次重建时都要重发：nftables 放行条目有 TTL（默认 300s），
 * 长连接断开期间条目可能已过期，不重新敲门就会出现"连不上但原因不明"。
 *
 * 返回：0 成功；>0 服务端结果码；<0 失败（-2 = UI 无法提供凭据）。
 */
#include "qc_keeper.h"

#include <stdio.h>
#include <stdlib.h>

#include "q_mem.h"

/* 解析 "ip:port" -> host/port。成功 0，失败 -1（不改动 host 内容） */
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

int qc_keeper_rebuild(struct qc_session *s)
{
    int rc;

    if (!s || !s->rc.host[0] || s->rc.port == 0)
        return -1;

    qc_close(s);

    /*
     * 关键：必须销毁旧 chan 再新建一个。
     * 1) 状态机不允许"已 KEYED/AUTHED 的 chan 再握手一次"（HANDSHAKE_OK 只接受 NEW/HELLO），
     *    复用会让重建永远失败 —— 表现为"重连一直连不上，但日志只有握手失败"；
     * 2) 一连接一会话：新 TCP 必须配新通道，channel_id 随之变化，旧 Token 自然失效，
     *    下面的重新登录不是可选项而是必须项。
     */
    if (s->chan)
    {
        q_chan_destroy(s->chan);
        s->chan = NULL;
    }
    s->chan = q_chan_create(Q_CHAN_MODE_CLIENT, NULL, 0, NULL, 0,
                            s->pub_der, s->pub_len, s->seq0, s->key_id);
    if (!s->chan)
        return -1;

    if (s->rc.knock[0] && s->rc.spa_key_len > 0u)
    {
        char    khost[64];
        int     kport = 0;
        uint8_t uid[32];
        size_t  ulen;

        if (split_ep(s->rc.knock, khost, sizeof(khost), &kport) == 0)
        {
            memset(uid, 0, sizeof(uid));
            ulen = strlen(s->rc.user);
            if (ulen > sizeof(uid))
                ulen = sizeof(uid);
            memcpy(uid, s->rc.user, ulen);

            /* 敲门失败也要继续尝试连接：可能是 knockd 未部署而端口本来就通 */
            (void)qc_knock(khost, kport, s->rc.spa_key, s->rc.spa_key_len,
                           s->rc.spa_key_id, uid, (uint16_t)s->rc.port, 300);
        }
    }

    rc = qc_connect(s, s->rc.host, (int)s->rc.port);
    if (rc != 0)
        return -1;

    rc = qc_hello(s);
    if (rc != 0)
        return (rc > 0) ? rc : -1;

    /* 连接重建后必须重新登录：Token 绑定 channel_id，新握手 = 新通道 = 旧 Token 无效 */
    return qc_keeper_reauth(s);
}
