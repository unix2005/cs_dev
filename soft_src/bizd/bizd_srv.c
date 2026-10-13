/**
 * @file bizd_srv.c
 * @brief bizd 服务器主体：UDS 监听、accept 派发、每连接 worker 会话
 *
 * 与 netd 对称：netd 是 UDS 客户端（q_ipc_connect），bizd 是 UDS 服务端
 * （q_ipc_listen + q_ipc_accept）。worker 内【按帧】处理请求/响应，
 * 不涉及加解密（数据已由 netd 解密后以可信 TLV 送达）。
 */
#include "srv_bizd.h"

/* 逐条解析外层 TLV 信封，填充连接上下文 */
static int parse_req(const uint8_t *buf, size_t len, bizd_conn_t *c)
{
    size_t off = 0u;
    int    got_cmd = 0;

    while (off < len)
    {
        uint16_t       tag = 0u, vlen = 0u;
        const uint8_t *val = NULL;
        size_t         used = 0u;

        if (q_codec_tlv_decode(buf + off, len - off, &tag, &val, &vlen, &used) != 0)
            break;
        off += used;

        switch (tag)
        {
        case Q_IPC_T_CMD:
            if (vlen >= 2u)
            {
                c->cmd    = bizd_get_be16(val);
                got_cmd   = 1;
            }
            break;
        case Q_IPC_T_BODY:
            c->body     = val;
            c->body_len = vlen;
            break;
        case Q_IPC_T_SRCIP:
            if (vlen >= 4u)
                memcpy(&c->src_ip, val, 4u);
            break;
        case Q_IPC_T_CHANID:
            if (vlen >= 32u)
                memcpy(c->channel_id, val, 32u);
            break;
        case Q_IPC_T_USERID:
            if (vlen >= 32u)
                memcpy(c->user_id, val, 32u);
            break;
        case Q_IPC_T_TOKEN:
            if (vlen >= 32u)
            {
                memcpy(c->token, val, 32u);
                c->has_token = 1;
            }
            break;
        default:
            break;
        }
    }
    return got_cmd ? 0 : -1;
}

/* worker：处理一条 UDS 连接上的一帧请求，回一帧响应 */
void bizd_conn_worker(void *arg)
{
    bizd_conn_t *c   = (bizd_conn_t *)arg;
    bizd_udata_t ud;
    uint8_t     *req = NULL, *resp = NULL;
    size_t       reqlen = 0u;

    req  = (uint8_t *)malloc(Q_IPC_MAXMSG);
    resp = (uint8_t *)malloc(Q_IPC_MAXMSG);
    if (!req || !resp)
        goto cleanup;

    (void)bizd_set_io_timeout(c->fd, BIZD_IO_TIMEOUT_SEC);

    if (bizd_recv_frame(c->fd, req, Q_IPC_MAXMSG, &reqlen) != 0)
        goto cleanup;

    if (parse_req(req, reqlen, c) != 0)
    {
        /* 结构非法：回 BADREQ */
        uint8_t rb[4];
        size_t  n = 0u;
        bizd_put_be32(rb, (uint32_t)Q_IPC_R_BADREQ);
        if (q_codec_tlv_encode(resp, Q_IPC_MAXMSG, Q_IPC_T_RESULT, rb, 4u, &n) == 0)
        {
            (void)bizd_send_frame(c->fd, resp, n);
        }
        goto cleanup;
    }

    ud.g = c->g;
    ud.c = c;

    /* 分发到下游微服务：out 中已是外层 TLV(RESULT)+TLV(BODY) */
    {
        size_t outlen = 0u;
        if (q_chan_disp_invoke(c->g->disp, c->cmd, c->body, c->body_len,
                               resp, Q_IPC_MAXMSG, &outlen) == 0 && outlen > 0u)
        {
            (void)bizd_send_frame(c->fd, resp, outlen);
        }
    }

cleanup:
    q_ipc_close(c->fd, NULL);
    free(req);
    free(resp);
    q_mem_zero(c, sizeof(*c));
    free(c);
}

/* accept 回调：取空本轮 UDS 连接，逐个派发到有界线程池 */
void bizd_on_accept(q_fd_t fd, uint32_t events, void *ctx)
{
    bizd_ctx_t *g = (bizd_ctx_t *)ctx;
    (void)events;

    for (;;)
    {
        int           cfd;
        bizd_conn_t  *c;

        if (q_ipc_accept((int)fd, &cfd) != 0)
            break;

        c = (bizd_conn_t *)calloc(1, sizeof(*c));
        if (!c)
        {
            q_ipc_close(cfd, NULL);
            continue;
        }
        c->fd = cfd;
        c->g  = g;

        if (q_tpool_dispatch(g->pool, bizd_conn_worker, c) != 0)
        {
            /* 队列满：拒绝新连接，停止本轮 accept 避免雪崩 */
            q_ipc_close(cfd, NULL);
            free(c);
            break;
        }
    }
}
