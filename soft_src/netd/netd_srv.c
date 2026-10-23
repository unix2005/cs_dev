/**
 * @file netd_srv.c
 * @brief netd 连接受理与每连接会话（阻塞式 q_chan 全流程）
 *
 * 与 q_net_srv 的差异：q_net_srv 会置连接 fd 为非阻塞且由 reactor 读空数据后按
 * chunk 派发，无法承载 q_chan 的阻塞多轮往返握手；故此处自建 accept 循环，
 * 并把【整条连接】派发到有界线程池，在 worker 内完成握手与收发。
 */
#include "srv_netd.h"

/* worker：一条连接的完整生命周期（握手 -> 收包 -> 校验 -> 转发 -> 回写） */
void conn_worker(void *arg)
{
    netd_conn_t   *c = (netd_conn_t *)arg;
    netd_ctx_t    *g = c->g;
    netd_udata_t   ud;
    q_chan_disp_t *disp = NULL;
    q_chan_t      *ch = NULL;
    uint8_t       *plain = NULL, *resp = NULL;
    char           ep[INET6_ADDRSTRLEN + 8];   /* 连接端点 "ip:port"，用于日志定位 */
    char           cid[INET6_ADDRSTRLEN + 128]; /* 跨服务对账标识 src_ip#req_id[(uid=...)] */

    plain = (uint8_t *)malloc(NETD_PLAIN_MAX);
    resp  = (uint8_t *)malloc(NETD_RESP_MAX);
    if (!plain || !resp)
        goto cleanup;

    /* 收发超时：阻塞式握手/收包若无超时，异常客户端可长期占用 worker */
    (void)netd_set_io_timeout(c->fd, NETD_IO_TIMEOUT_SEC);

    /* 指令分发表按连接创建（ud 指向本连接上下文，避免跨连接共享状态） */
    disp = q_chan_disp_create();
    if (!disp)
        goto cleanup;
    ud.g = g;
    ud.c = c;
    if (netd_disp_init(disp, &ud) != 0)
        goto cleanup;

    /* ---- 1. 握手（阻塞：收对端临时公钥 DER -> 派生会话密钥 -> 发本端 DER） ---- */
    ch = q_chan_create(Q_CHAN_MODE_SERVER, g->sm2_priv, g->sm2_priv_len,
                       g->sm2_pub, g->sm2_pub_len, NULL, 0, NETD_SEQ0, g->key_id);
    if (!ch)
    {
        QLOG_DEBUG(g->log, "通道创建失败 src=%s", netd_conn_ntop(c, ep, sizeof(ep)));
        goto cleanup;
    }
    if (q_chan_handshake(ch, c->fd) != 0)
    {
        QLOG_DEBUG(g->log, "握手失败 src=%s", netd_conn_ntop(c, ep, sizeof(ep)));
        goto cleanup;
    }
    if (q_chan_binding(ch, c->channel_id) != 0)
    {
        QLOG_DEBUG(g->log, "通道绑定失败 src=%s", netd_conn_ntop(c, ep, sizeof(ep)));
        goto cleanup;
    }
    /* q_chan.h 约定：握手成功后须由本函数（q_chan_handshake 的调用方）把连接态推进到 KEYED */
    if (q_chan_sm_transition(&c->st, Q_CHAN_EV_HANDSHAKE_OK) != 0)
    {
        QLOG_DEBUG(g->log, "状态迁移失败 NEW->KEYED src=%s",
                   netd_conn_ntop(c, ep, sizeof(ep)));
        goto cleanup;
    }
    c->chan = ch;      /* M5：读线程借它把 pushd 的推送帧加密回写（见 netd_push.c） */
    QLOG_INFO(g->log, "通道就绪 src=%s", netd_conn_ntop(c, ep, sizeof(ep)));

    /* ---- 2. 收包循环：阻塞式整帧收发 + 状态 / 配额校验 ---- */
    while (!daemon_should_stop())
    {
        size_t   plainlen = 0u, outlen = 0u;
        uint16_t cmd = 0u;
        int      login_ok = 0;

        if (q_chan_receive(ch, c->fd, plain, NETD_PLAIN_MAX, &plainlen, &cmd) != 0)
        {
            /* M5：推送连接（BEGIN 后）无心跳，收包超时是常态而非故障 ——
               与客户端 qc_push_thread 同策略：EAGAIN 继续等，仅真故障才拆链。
               若一律断连，一条健康的推送连接会每 IO 超时被误杀一次。 */
            if (c->push.fd >= 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            break;                              /* EOF / 校验失败 / 超时（非推送连接） */
        }

        /* 每帧一个请求号：随 IPC 透传到 bizd/微服务，保证整条链 src_ip#id 一致 */
        netd_next_req_id(c->req_id, sizeof(c->req_id));

        QLOG_INFO(g->log, "[recv] %s cmd=%u <-- 客户端",
                  netd_cid(c, cid, sizeof(cid)), (unsigned)cmd);

        /* 未认证阶段配额（§6.4）：包数 / 字节 / 存续时长 */
        c->pkts++;
        c->bytes += plainlen;
        if (c->st != Q_CHAN_ST_AUTHED)
        {
            if (c->pkts > NETD_UNAUTH_PKTS || c->bytes > NETD_UNAUTH_BYTES ||
                (time_t)now_sec() - c->accept_ts > (time_t)NETD_UNAUTH_SEC)
            {
                QLOG_DEBUG(g->log, "未认证配额超限，断连 src=%s",
                           netd_conn_ntop(c, ep, sizeof(ep)));
                break;
            }
        }

        /* 先按白名单裁决，非法指令直接断连（§5.3） */
        if (q_chan_sm_cmd_allowed(c->st, cmd) != 1)
        {
            QLOG_DEBUG(g->log, "指令 %u 在当前状态非法，断连 src=%s",
                       (unsigned)cmd, netd_conn_ntop(c, ep, sizeof(ep)));
            break;
        }

        /* LOGIN 需先得出 login_ok，再交给状态机裁决 */
        if (cmd == Q_CHAN_CMD_LOGIN)
        {
            login_ok = do_login(c, plain, plainlen, resp, NETD_RESP_MAX, &outlen);
            if (!login_ok)
                fail_note(g, &c->src_addr);   /* 计入 IP 维度登录失败配额（§6.4） */
        }

        if (q_chan_sm_on_cmd(&c->st, cmd, login_ok) != 0)
        {
            QLOG_DEBUG(g->log, "指令 %u 状态迁移失败，断连 src=%s",
                       (unsigned)cmd, netd_conn_ntop(c, ep, sizeof(ep)));
            break;
        }

        /* 其余指令走分发表 */
        if (cmd != Q_CHAN_CMD_LOGIN)
        {
            if (q_chan_disp_invoke(disp, cmd, plain, plainlen, resp,
                                   NETD_RESP_MAX, &outlen) != 0)
            {
                QLOG_DEBUG(g->log, "指令 %u 处理失败 src=%s",
                           (unsigned)cmd, netd_conn_ntop(c, ep, sizeof(ep)));
                break;
            }
        }

        /* 回写一律走 netd_push_transmit：持 push.tx_lock 串行化。
           推送连接上读线程也在同一通道回写 TRAJ/ALARM，而 q_chan_transmit
           无内部锁且会改 seq_send —— 不串行化会出现 seq 错乱/密文错帧。 */
        if (outlen > 0u && netd_push_transmit(c, resp, outlen, cmd) != 0)
            break;

        QLOG_INFO(g->log, "[send] %s cmd=%u --> 客户端 outlen=%zu",
                  netd_cid(c, cid, sizeof(cid)), (unsigned)cmd, outlen);
    }

cleanup:
    /* M5：先拆推送反向通路（停读线程 -> join -> 关 UDS）。
       必须在关客户端 fd 之前：join 保证读线程不再触碰 c->fd。 */
    push_teardown(c);
    if (ch)
        q_chan_destroy(ch);          /* 内部清零私钥 / 会话密钥 */
    if (disp)
        q_chan_disp_destroy(disp);
    if (plain)
    {
        q_mem_zero(plain, NETD_PLAIN_MAX);
        free(plain);
    }
    if (resp)
    {
        q_mem_zero(resp, NETD_RESP_MAX);
        free(resp);
    }
    conn_release(g, &c->src_addr);
    q_net_close(c->fd);
    q_mem_zero(c, sizeof(*c));
    free(c);
}

/* accept 回调：取空本轮连接，逐个准入校验后派发 */
void on_accept(q_fd_t fd, uint32_t events, void *ctx)
{
    netd_ctx_t *g = (netd_ctx_t *)ctx;
    (void)events;

    for (;;)
    {
        struct sockaddr_storage ss;
        socklen_t  slen     = sizeof(ss);
        int        cfd      = (int)accept((int)fd, (struct sockaddr *)&ss, &slen);
        netd_addr_t src_addr;
        uint16_t   src_port = 0u;
        netd_conn_t *c;

        memset(&src_addr, 0, sizeof(src_addr));
        if (cfd < 0)
        {
            if (errno == EINTR)
                continue;
            break;                       /* EAGAIN：本轮连接已取空 */
        }

        if (ss.ss_family == AF_INET)
        {
            struct sockaddr_in *a = (struct sockaddr_in *)&ss;
            src_addr.fam = AF_INET;
            memcpy(src_addr.b, &a->sin_addr.s_addr, 4);  /* 网络序字节 */
            src_port = a->sin_port;
        }
        else if (ss.ss_family == AF_INET6)
        {
            /* IPv6 源 IP 同样作为限流键（§6.4 二期）：写入 16 字节地址，
               与 IPv4 统一参与 IP 维度并发/新建速率/失败拉黑区分。 */
            struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&ss;
            src_addr.fam = AF_INET6;
            memcpy(src_addr.b, &a6->sin6_addr.s6_addr, 16);
            src_port = a6->sin6_port;
        }

        /* 准入：全局并发 + 单 IP 并发 + 拉黑 + 新建速率 */
        if (conn_acquire(g, &src_addr) != 0)
        {
            q_net_close((q_fd_t)cfd);
            continue;
        }

        c = (netd_conn_t *)calloc(1, sizeof(*c));
        if (!c)
        {
            conn_release(g, &src_addr);
            q_net_close((q_fd_t)cfd);
            continue;
        }
        c->fd        = (q_fd_t)cfd;
        c->src_addr  = src_addr;
        c->src_port  = src_port;
        c->st        = Q_CHAN_ST_NEW;
        c->accept_ts = (time_t)now_sec();
        c->g         = g;

        if (q_tpool_dispatch(g->pool, conn_worker, c) != 0)
        {
            /* 队列满：拒绝并回收配额，停止本轮 accept 避免雪崩 */
            conn_release(g, &src_addr);
            q_net_close(c->fd);
            free(c);
            break;
        }
    }
}
