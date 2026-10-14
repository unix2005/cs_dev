/**
 * @file netd_ipc.c
 * @brief netd -> bizd 的 Unix Socket 转发（q_ipc 之上自建分帧）
 *
 * wire format: [4 字节大端长度][TLV 序列]
 *   q_ipc 是纯字节管道、无帧格式，且 q_ipc_recv 为单次 recv（可能短读），
 *   故消息分帧由本文件负责。
 */
#include "srv_netd.h"

static int ipc_read_exact(int fd, uint8_t *buf, size_t len)
{
    size_t got = 0u;

    while (got < len)
    {
        ssize_t n = q_ipc_recv(fd, buf + got, len - got);
        if (n <= 0)
            return -1;               /* 0=对端关闭，<0 出错 */
        got += (size_t)n;
    }
    return 0;
}

/* 全局请求号计数器：netd 为客户端入站侧，由本进程统一生成，
   经 IPC REQID TLV 透传到 bizd -> 微服务，保证整条链 src_ip#id 一致。
   复用 g_netd.lock（已初始化，原用于配额计数），避免新增锁。 */
void netd_next_req_id(char *buf, size_t cap)
{
    unsigned long long n;

    pthread_mutex_lock(&g_netd.lock);
    static uint64_t g_netd_req_seq = 0;
    n = (unsigned long long)(++g_netd_req_seq);
    pthread_mutex_unlock(&g_netd.lock);

    if (buf && cap)
        snprintf(buf, cap, "%llu", n);
}

/* 请求：  [len:4][TLV(CMD)][TLV(BODY)][TLV(SRC_IP)][TLV(CHANID)][TLV(USERID)][TLV(REQID)] */
static size_t ipc_build_req(uint8_t *out, size_t outcap, uint16_t cmd,
                            const uint8_t *body, size_t blen,
                            const netd_addr_t *src, const uint8_t chanid[32],
                            const uint8_t userid[32], const char *reqid)
{
    size_t off = Q_IPC_LENPFX, n = 0u;
    uint8_t u16[2], ipb[17];
    uint8_t fam = (src->fam == AF_INET6) ? 6u : 4u;

    put_be16(u16, cmd);
    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_CMD, u16, 2, &n) == 0)
        off += n;

    if (blen > 0u)
    {
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_BODY, body,
                               (uint16_t)blen, &n) == 0)
            off += n;
    }

    /* SRCIP 扩为 [1B 族][16B 地址]（IPv4 时地址占低 4 字节），支持 IPv6 溯源 */
    memset(ipb, 0, sizeof(ipb));
    ipb[0] = fam;
    memcpy(ipb + 1u, src->b, (fam == 6u) ? 16u : 4u);
    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_SRCIP, ipb, 17, &n) == 0)
        off += n;

    if (chanid && q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_CHANID,
                                     chanid, 32, &n) == 0)
        off += n;

    if (userid && q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_USERID,
                                     userid, 32, &n) == 0)
        off += n;

    if (reqid && reqid[0])
    {
        size_t rl = strlen(reqid);
        if (rl > 0u && rl <= (size_t)UINT16_MAX &&
            q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_REQID,
                               (const uint8_t *)reqid, (uint16_t)rl, &n) == 0)
            off += n;
    }

    put_be32(out, (uint32_t)(off - Q_IPC_LENPFX));
    return off;
}

/*
 * 把已认证的业务请求转发给 bizd，并取回响应。
 * 返回 0 成功（*result 为 bizd 结果码）；非 0 表示本层无法完成转发。
 */
int ipc_forward(netd_ctx_t *g, const netd_addr_t *src, uint16_t cmd,
                const uint8_t *body, size_t blen,
                const uint8_t chanid[32], const uint8_t userid[32],
                const char *reqid,
                uint32_t *result, uint8_t *out, size_t outcap, size_t *out_len)
{
    uint8_t  req[Q_IPC_LENPFX + 3u * (4u + Q_IPC_TLV_MAX) + 64u];
    uint8_t *rsp = NULL;
    uint8_t  lenbuf[Q_IPC_LENPFX];
    uint8_t  rb[4];
    size_t   reqlen, rsplen = 0u, off = 0u, n = 0u;
    const uint8_t *rbody = NULL, *rtok = NULL;
    uint16_t tag = 0u, vlen = 0u, rblen = 0u;
    const uint8_t *val = NULL;
    size_t used = 0u;
    int    fd = -1, rc = -1;

    *out_len = 0u;
    if (!result)
        return -1;
    *result = Q_IPC_R_UNAVAILABLE;

    if (blen > Q_IPC_TLV_MAX)
    {
        QLOG_WARN(g->log, "业务体超限 %lu > %u，拒绝转发", (unsigned long)blen,
                  (unsigned)Q_IPC_TLV_MAX);
        return -1;
    }

    reqlen = ipc_build_req(req, sizeof(req), cmd, body, blen, src,
                           chanid, userid, reqid);

    if (q_ipc_connect(g_cfg.ipc_path, &fd) != 0)
    {
        QLOG_WARN(g->log, "连接 bizd 失败: %s", g_cfg.ipc_path);
        return -1;
    }

    if (q_ipc_send(fd, req, reqlen) != (ssize_t)reqlen)
    {
        QLOG_WARN(g->log, "向 bizd 发送请求失败");
        goto out;
    }

    /* 读 4 字节长度前缀 */
    if (ipc_read_exact(fd, lenbuf, Q_IPC_LENPFX) != 0)
    {
        QLOG_WARN(g->log, "读取 bizd 响应长度失败");
        goto out;
    }
    rsplen = (size_t)(((uint32_t)lenbuf[0] << 24) | ((uint32_t)lenbuf[1] << 16) |
                      ((uint32_t)lenbuf[2] << 8) | (uint32_t)lenbuf[3]);
    if (rsplen == 0u || rsplen > Q_IPC_MAXMSG)
    {
        QLOG_WARN(g->log, "bizd 响应长度非法: %lu", (unsigned long)rsplen);
        goto out;
    }

    rsp = (uint8_t *)malloc(rsplen);
    if (!rsp)
        goto out;
    if (ipc_read_exact(fd, rsp, rsplen) != 0)
    {
        QLOG_WARN(g->log, "读取 bizd 响应体失败");
        goto out;
    }

    /* 解析响应 TLV：RESULT / BODY / TOKEN */
    while (off < rsplen)
    {
        if (q_codec_tlv_decode(rsp + off, rsplen - off, &tag, &val, &vlen, &used) != 0)
            break;
        off += used;
        if (tag == Q_IPC_T_RESULT && vlen >= 4u)
        {
            *result = (uint32_t)(((uint32_t)val[0] << 24) | ((uint32_t)val[1] << 16) |
                       ((uint32_t)val[2] << 8) | (uint32_t)val[3]);
        }
        else if (tag == Q_IPC_T_BODY)
        {
            rbody = val;
            rblen = vlen;
        }
        else if (tag == Q_IPC_T_TOKEN && vlen >= Q_CHAN_TOKEN_LEN)
        {
            rtok = val;
        }
    }

    /* 组装回给客户端的响应体：TLV(RESULT) [+ TLV(BODY)] [+ TLV(TOKEN)] */
    off = 0u;
    n   = 0u;
    put_be32(rb, *result);
    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_RESULT, rb, 4, &n) == 0)
        off += n;

    if (rbody && rblen > 0u)
    {
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_BODY, rbody, rblen, &n) == 0)
            off += n;
    }
    if (rtok)
    {
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_TOKEN, rtok,
                               Q_CHAN_TOKEN_LEN, &n) == 0)
            off += n;
    }
    *out_len = off;

    rc = 0;
out:
    if (fd >= 0)
        q_ipc_close(fd, NULL);
    if (rsp)
        free(rsp);
    return rc;
}
