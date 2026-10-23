/**
 * @file netd_push.c
 * @brief M5 推送反向通路：netd_B <-> pushd 持久 UDS + 读线程（TRAJ/ALARM 加密回写）
 *
 * 数据流（与 biz 的"一问一答"根本不同，pushd 是服务端主动推）：
 *   客户端 --PUSH_BEGIN--> netd_B --BEGIN--> pushd
 *      netd_B 为这条推送连接【独占】一条到 pushd 的 UDS（1:1，故 pushd 来的帧
 *      无须带 CHANID —— UDS 连接本身就是会话上下文，见 srv_push.h 文件头）。
 *   客户端 --PUSH_SUB_DELTA--> netd_B --SUB_DELTA--> pushd（免回执）
 *   pushd --TRAJ/ALARM--> netd_B 读线程 --q_chan_transmit--> 客户端（加密）
 *
 * 并发模型：
 *   - worker 线程：收客户端指令、回 BEGIN 的 RESULT；
 *   - 读线程：阻塞收 pushd，推送帧加密回写。
 *   两者都会调 q_chan_transmit（无内部锁、会改 seq_send），故所有 transmit
 *   走 netd_push_transmit() 持 c->push.tx_lock 串行化。
 *   - UDS 的写只有 worker 一个入口（SUB_DELTA 透传），读线程只读不写，
 *     故 UDS 收发本身不需要额外锁。
 *
 * 拆链顺序（worker cleanup，push_teardown）：
 *   置 stop -> shutdown(UDS) 唤醒读线程 -> join（保证读线程不再碰客户端 fd）
 *   -> 关 UDS -> （worker 继续）关客户端 fd。反向：读线程先发现 UDS 断开时，
 *   shutdown(客户端 fd) 唤醒阻塞中的 worker，由 worker 走同一套 cleanup。
 */
#include "srv_netd.h"

#include <errno.h>
#include <pthread.h>

/* 推送帧缓冲：单帧明文 <= NETD_PLAIN_MAX（与 conn_worker 同口径）；
   pushd 侧单帧上限 PUSH_MAXMSG=65536，这里取 64KB+余量对齐 Q_IPC_MAXMSG。 */
#define PUSH_FRAME_MAX Q_IPC_MAXMSG

/* 读线程循环的 recv 超时：与 worker 一致，超时后检查 stop 标志，
   保证 daemon 退出时读线程有界join（不会无限阻塞关不慢）。 */
#define PUSH_IO_TIMEOUT_SEC NETD_IO_TIMEOUT_SEC

/* keepalive 参数（秒）：推送连接无心跳，半开连接（对端宕机无 FIN）只能靠
   内核探活兜底。与客户端 qc_push_thread.c 的激进参数保持一致（45/10/3）。 */
#define PUSH_KA_IDLE  45
#define PUSH_KA_INTVL 10
#define PUSH_KA_CNT   3

static int ipc_read_exact(q_fd_t fd, uint8_t *buf, size_t len)
{
    size_t got = 0u;

    while (got < len)
    {
        ssize_t n = q_ipc_recv((int)fd, buf + got, len - got);
        if (n <= 0)
            return -1;               /* 0=对端关闭，<0 出错/超时 */
        got += (size_t)n;
    }
    return 0;
}

/* 推送连接启用 TCP keepalive（尽力而为，失败不影响主流程） */
static void push_set_keepalive(q_fd_t fd)
{
    int on = 1;

    if (fd == (q_fd_t)-1)
        return;
#ifdef Q_SYS_WINDOWS
    {
        struct tcp_keepalive ka;
        DWORD nbytes = 0;

        if (setsockopt((SOCKET)fd, SOL_SOCKET, SO_KEEPALIVE,
                       (const char *)&on, sizeof(on)) != 0)
            return;
        ka.onoff             = 1;
        ka.keepalivetime     = PUSH_KA_IDLE * 1000;
        ka.keepaliveinterval = PUSH_KA_INTVL * 1000;
        (void)WSAIoctl((SOCKET)fd, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
                       NULL, 0, &nbytes, NULL);
    }
#else
    int idle = PUSH_KA_IDLE, intvl = PUSH_KA_INTVL, cnt = PUSH_KA_CNT;

    if (setsockopt((int)fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) != 0)
        return;
#if defined(TCP_KEEPIDLE)
    (void)setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#if defined(TCP_KEEPINTVL)
    (void)setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#if defined(TCP_KEEPCNT)
    (void)setsockopt((int)fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
#endif
}

/* [4B BE len][payload] 定长帧发送（q_ipc_send 已处理短写/EINTR） */
static int push_ipc_send_frame(q_fd_t fd, const uint8_t *buf, size_t len)
{
    uint8_t pfx[Q_IPC_LENPFX];

    if (len == 0u || len > 0xFFFFFFFFu)
        return -1;
    put_be32(pfx, (uint32_t)len);
    if (q_ipc_send((int)fd, pfx, Q_IPC_LENPFX) != (ssize_t)Q_IPC_LENPFX)
        return -1;
    if (q_ipc_send((int)fd, buf, len) != (ssize_t)len)
        return -1;
    return 0;
}

/* 收一帧 [4B BE len][TLV...]；超时/EOF/错误一律 -1（调用方据 stop 标志区分） */
static int push_ipc_recv_frame(q_fd_t fd, uint8_t *buf, size_t cap, size_t *outlen)
{
    uint8_t  pfx[Q_IPC_LENPFX];
    uint32_t need;

    if (ipc_read_exact(fd, pfx, Q_IPC_LENPFX) != 0)
        return -1;
    need = ((uint32_t)pfx[0] << 24) | ((uint32_t)pfx[1] << 16) |
           ((uint32_t)pfx[2] << 8) | (uint32_t)pfx[3];
    if (need == 0u || need > cap)
        return -1;
    if (ipc_read_exact(fd, buf, need) != 0)
        return -1;
    if (outlen)
        *outlen = need;
    return 0;
}

/* 构造 netd_B -> pushd 的 C→S 帧：TLV(CMD) + TLV(CHANID) [+ TLV(BODY)] [+ TLV(SRCIP/USERID/TOKEN/USERNAME)]
   返回帧长；0 = 编码失败。 */
static size_t push_build_frame(uint8_t *out, size_t outcap, uint16_t cmd,
                               const netd_conn_t *c,
                               const uint8_t *body, size_t blen)
{
    size_t  off = Q_IPC_LENPFX, n = 0u;
    uint8_t u16[2], ipb[17];
    uint8_t fam = (c->src_addr.fam == AF_INET6) ? 6u : 4u;

    put_be16(u16, cmd);
    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_CMD, u16, 2, &n) != 0)
        return 0u;
    off += n;

    if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_CHANID,
                           c->channel_id, 32, &n) != 0)
        return 0u;
    off += n;

    if (blen > 0u && body)
    {
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_BODY, body,
                               (uint16_t)blen, &n) != 0)
            return 0u;
        off += n;
    }

    /* BEGIN 的鉴权/对账材料：SRCIP / USERID / TOKEN / USERNAME（SUB_DELTA 不带） */
    if (cmd == Q_CHAN_CMD_PUSH_BEGIN)
    {
        memset(ipb, 0, sizeof(ipb));
        ipb[0] = fam;
        memcpy(ipb + 1u, c->src_addr.b, (fam == 6u) ? 16u : 4u);
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_SRCIP, ipb, 17, &n) != 0)
            return 0u;
        off += n;
        if (q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_USERID,
                               c->token.user_id, 32, &n) != 0)
            return 0u;
        off += n;
        if (c->has_token &&
            q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_TOKEN,
                               c->token.token, Q_CHAN_TOKEN_LEN, &n) != 0)
            return 0u;
        off += n;
        if (c->user_name[0] &&
            q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_USERNAME,
                               (const uint8_t *)c->user_name,
                               (uint16_t)strlen(c->user_name), &n) != 0)
            return 0u;
        off += n;
    }

    put_be32(out, (uint32_t)(off - Q_IPC_LENPFX));
    return off;
}

int netd_push_transmit(netd_conn_t *c, const uint8_t *plain, size_t plen, uint16_t cmd)
{
    int rc;

    if (!c || !c->chan)
        return -1;
    if (c->push.tx_lock_inited)
        pthread_mutex_lock(&c->push.tx_lock);
    rc = q_chan_transmit(c->chan, c->fd, plain, plen, cmd);
    if (c->push.tx_lock_inited)
        pthread_mutex_unlock(&c->push.tx_lock);
    return rc;
}

/* 读线程：阻塞收 pushd 的推送帧，解出 CMD+BODY 后加密回写客户端。
   UDS 断开（pushd 重启/崩溃）=> shutdown 客户端 fd 唤醒 worker 走统一拆链。 */
static void *push_reader_thread(void *arg)
{
    netd_conn_t *c = (netd_conn_t *)arg;
    uint8_t     *buf = (uint8_t *)malloc(PUSH_FRAME_MAX);
    char         cid[INET6_ADDRSTRLEN + 128];

    if (!buf)
    {
        /* 内存耗尽：没有缓冲就读不了帧，只能拆链（客户端会重连） */
        shutdown((int)c->fd, SHUT_RDWR);
        return NULL;
    }

    (void)netd_set_io_timeout(c->push.fd, PUSH_IO_TIMEOUT_SEC);

    while (!c->push.stop)
    {
        size_t   flen = 0u, off = 0u;
        uint16_t cmd = 0u;
        const uint8_t *body = NULL;
        uint16_t tag = 0u, vlen = 0u, blen = 0u;
        const uint8_t *val = NULL;
        size_t   used = 0u;

        if (push_ipc_recv_frame(c->push.fd, buf, PUSH_FRAME_MAX, &flen) != 0)
        {
            /* 超时（EAGAIN）是常态而非故障：订阅为空（还没订 / 已全部 REMOVE）时
               pushd 本来就不发帧，与客户端 qc_push_thread "无数据是常态" 同策略。
               只有真 EOF/错误才拆链 —— 否则一个订了空集的客户端 10s 后被误杀。 */
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            break;                  /* EOF / 错误：UDS 链路已断 */
        }

        while (off < flen)
        {
            if (q_codec_tlv_decode(buf + off, flen - off, &tag, &val, &vlen, &used) != 0)
                break;
            off += used;
            if (tag == Q_IPC_T_CMD && vlen >= 2u)
                cmd = (uint16_t)(((uint16_t)val[0] << 8) | val[1]);
            else if (tag == Q_IPC_T_BODY)
            {
                body = val;
                blen = vlen;
            }
        }

        if ((cmd == Q_CHAN_CMD_PUSH_TRAJ || cmd == Q_CHAN_CMD_PUSH_ALARM) &&
            body && blen > 0u)
        {
            if (netd_push_transmit(c, body, blen, cmd) != 0)
                break;                  /* 写客户端失败：拆链 */
        }
        /* 其它 cmd：忽略但不断 UDS（pushd 契约外的东西，防御性处理） */
    }

    if (!c->push.stop)
    {
        /* 非主动停止 = UDS 链路已断：唤醒 worker（它可能阻塞在客户端 recv），
           由 worker 走统一 cleanup（join 本线程 -> 关 UDS -> 关客户端 fd）。 */
        QLOG_WARN(c->g->log, "pushd 链路断开（%s），关闭推送连接等客户端重建",
                  netd_cid(c, cid, sizeof(cid)));
        shutdown((int)c->fd, SHUT_RDWR);
    }

    if (buf)
    {
        q_mem_zero(buf, PUSH_FRAME_MAX);
        free(buf);
    }
    return NULL;
}

int push_begin(netd_conn_t *c, uint32_t *result)
{
    uint8_t  req[Q_IPC_LENPFX + 3u * (4u + Q_IPC_TLV_MAX) + 64u];
    uint8_t  rsp[64];
    size_t   reqlen, rsplen = 0u;
    uint32_t r = Q_IPC_R_UNAVAILABLE;
    int      fd = -1;
    char     cid[INET6_ADDRSTRLEN + 128];

    if (result)
        *result = Q_IPC_R_UNAVAILABLE;
    if (!c || !c->g)
        return -1;

    /* 本实例未配置 push_ipc_path：推送整条禁用（netd_A 的常态） */
    if (!g_cfg.push_ipc_path[0])
    {
        if (result)
            *result = Q_IPC_R_UNSUPPORTED;
        return -1;
    }

    /* 重复 BEGIN：幂等应答（客户端重连时会先停后起，正常不会到这里） */
    if (c->push.fd >= 0)
    {
        if (result)
            *result = Q_IPC_R_OK;
        return 0;
    }

    if (q_ipc_connect(g_cfg.push_ipc_path, &fd) != 0)
    {
        QLOG_WARN(c->g->log, "连接 pushd 失败: %s（%s）",
                  g_cfg.push_ipc_path, netd_cid(c, cid, sizeof(cid)));
        if (result)
            *result = Q_IPC_R_UNAVAILABLE;
        return -1;
    }
    (void)netd_set_io_timeout((q_fd_t)fd, PUSH_IO_TIMEOUT_SEC);

    reqlen = push_build_frame(req, sizeof(req), Q_CHAN_CMD_PUSH_BEGIN, c, NULL, 0u);
    if (reqlen == 0u || push_ipc_send_frame((q_fd_t)fd, req, reqlen) != 0)
    {
        QLOG_WARN(c->g->log, "向 pushd 发送 BEGIN 失败（%s）",
                  netd_cid(c, cid, sizeof(cid)));
        goto fail;
    }

    /* lockstep：读 RESULT 应答（srv_push.h：BEGIN 有应答、SUB_DELTA 免回执） */
    if (push_ipc_recv_frame((q_fd_t)fd, rsp, sizeof(rsp), &rsplen) != 0)
    {
        QLOG_WARN(c->g->log, "读取 pushd BEGIN 应答失败（%s）",
                  netd_cid(c, cid, sizeof(cid)));
        goto fail;
    }
    {
        size_t   off = 0u;
        uint16_t tag = 0u, vlen = 0u;
        const uint8_t *val = NULL;
        size_t   used = 0u;
        int      got = 0;

        while (off < rsplen)
        {
            if (q_codec_tlv_decode(rsp + off, rsplen - off, &tag, &val, &vlen, &used) != 0)
                break;
            off += used;
            if (tag == Q_IPC_T_RESULT && vlen >= 4u)
            {
                r = ((uint32_t)val[0] << 24) | ((uint32_t)val[1] << 16) |
                    ((uint32_t)val[2] << 8) | (uint32_t)val[3];
                got = 1;
            }
        }
        if (!got)
        {
            QLOG_WARN(c->g->log, "pushd BEGIN 应答缺 RESULT（%s）",
                      netd_cid(c, cid, sizeof(cid)));
            goto fail;
        }
    }
    if (r != Q_IPC_R_OK)
    {
        QLOG_WARN(c->g->log, "pushd 拒绝推送会话 result=%u（%s）",
                  (unsigned)r, netd_cid(c, cid, sizeof(cid)));
        if (result)
            *result = r;
        goto fail;
    }

    /* BEGIN 成功：装配本连接的推送状态并起读线程。
       顺序要点：先由 worker 把 RESULT=OK 发给客户端，再起读线程 ——
       保证客户端收到的第一条推送连接帧一定是 BEGIN 应答（TCP 有序）。 */
    if (!c->push.tx_lock_inited &&
        pthread_mutex_init(&c->push.tx_lock, NULL) != 0)
    {
        QLOG_WARN(c->g->log, "push tx_lock 初始化失败（%s）",
                  netd_cid(c, cid, sizeof(cid)));
        goto fail;
    }
    c->push.tx_lock_inited = 1;
    c->push.fd             = (q_fd_t)fd;
    c->push.stop           = 0;

    /* 推送连接无心跳：客户端 fd 启用 keepalive 兜底半开探测 */
    push_set_keepalive(c->fd);

    if (pthread_create(&c->push.reader, NULL, push_reader_thread, c) != 0)
    {
        QLOG_WARN(c->g->log, "push 读线程创建失败（%s）",
                  netd_cid(c, cid, sizeof(cid)));
        c->push.fd = (q_fd_t)-1;
        goto fail;
    }
    c->push.reader_on = 1;

    QLOG_INFO(c->g->log, "push 会话建立（%s user=%s）",
              netd_cid(c, cid, sizeof(cid)), c->user_name[0] ? c->user_name : "?");
    if (result)
        *result = Q_IPC_R_OK;
    return 0;

fail:
    if (fd >= 0)
        q_ipc_close(fd, NULL);
    return -1;
}

void push_forward_delta(netd_conn_t *c, const uint8_t *body, size_t blen)
{
    uint8_t req[Q_IPC_LENPFX + 3u * (4u + Q_IPC_TLV_MAX) + 64u];
    size_t  reqlen;
    char    cid[INET6_ADDRSTRLEN + 128];

    if (!c || c->push.fd < 0 || !body || blen == 0u)
        return;                         /* 未建 UDS：静默丢弃（订阅集合以本地为真相） */

    reqlen = push_build_frame(req, sizeof(req), Q_CHAN_CMD_PUSH_SUB_DELTA, c, body, blen);
    if (reqlen == 0u)
        return;
    if (push_ipc_send_frame(c->push.fd, req, reqlen) != 0)
    {
        /* 写失败：UDS 可能已断。不在此处拆链（读线程会发现并 shutdown 客户端），
           仅告警；本地集合仍是真相，重连后全量重放兜底。 */
        QLOG_WARN(c->g->log, "向 pushd 转发 SUB_DELTA 失败（%s）",
                  netd_cid(c, cid, sizeof(cid)));
    }
}

void push_teardown(netd_conn_t *c)
{
    if (!c)
        return;

    if (c->push.fd >= 0)
    {
        c->push.stop = 1;
        shutdown((int)c->push.fd, SHUT_RDWR);   /* 唤醒阻塞中的读线程 */
        if (c->push.reader_on)
        {
            pthread_join(c->push.reader, NULL);
            c->push.reader_on = 0;
        }
        q_ipc_close((int)c->push.fd, NULL);     /* UDS 会话 fd：不传 path */
        c->push.fd = (q_fd_t)-1;
    }
    if (c->push.tx_lock_inited)
    {
        pthread_mutex_destroy(&c->push.tx_lock);
        c->push.tx_lock_inited = 0;
    }
}
