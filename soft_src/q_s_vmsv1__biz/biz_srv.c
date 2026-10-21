/**
 * @file biz_srv.c
 * @brief 业务微服务服务器：reactor 监听 accept + 线程池每连接 worker + 定长帧收发
 *
 * 连接模型对齐 bizd 下游用法：bizd 每请求新建一条 TCP 连接，发一帧、等一帧响应后关闭。
 * 监听 socket 为阻塞，故 on_accept 每次仅 accept 一次（epoll 电平触发会再次回调）。
 */
#include "srv_biz.h"

/* 日志中单条消息展示的最大长度（超过则截断，避免超长日志行刷屏） */
#define BIZ_LOG_CAP 2048u

/* 从请求 JSON 提取“连接/请求标识”：socket 对端永远是 bizd(127.0.0.1)，无法区分用户，
   真实客户端身份由 bizd 透传在 ctx.src_ip。格式：<src_ip>#<id>[(uid=<64hex>)] */
static void biz_req_cid(const char *msg, char *out, size_t outsz)
{
    cJSON *root, *ctx, *ip, *id, *uid;
    const char *ip_s = NULL, *id_s = NULL, *uid_s = NULL;
    int    n;

    if (outsz == 0u)
        return;
    out[0] = '\0';
    root = cJSON_Parse(msg);
    if (!root)
    {
        snprintf(out, outsz, "?");
        return;
    }
    id  = cJSON_GetObjectItem(root, "id");
    ctx = cJSON_GetObjectItem(root, "ctx");
    if (id && cJSON_IsString(id))
        id_s = id->valuestring;
    if (ctx && cJSON_IsObject(ctx))
    {
        ip  = cJSON_GetObjectItem(ctx, "src_ip");
        uid = cJSON_GetObjectItem(ctx, "user_id");
        if (ip  && cJSON_IsString(ip)  && ip->valuestring && ip->valuestring[0])
            ip_s = ip->valuestring;
        if (uid && cJSON_IsString(uid) && uid->valuestring && uid->valuestring[0])
            uid_s = uid->valuestring;
    }
    n = snprintf(out, outsz, "%s#%s", ip_s ? ip_s : "?", id_s ? id_s : "");
    if ((size_t)n < outsz && uid_s)
        snprintf(out + n, outsz - (size_t)n, "(uid=%.16s)", uid_s);
    cJSON_Delete(root);
}

/* 把消息裁剪到 BIZ_LOG_CAP 用于日志展示；超长追加 "..." */
static const char *biz_msg_clip(const char *msg, char *tmp, size_t tmpsz)
{
    size_t len = strlen(msg);
    size_t c;

    if (len <= BIZ_LOG_CAP)
        return msg;
    if (tmpsz < 4u)
        return msg;
    c = (BIZ_LOG_CAP < tmpsz - 4u) ? BIZ_LOG_CAP : (tmpsz - 4u);
    memcpy(tmp, msg, c);
    memcpy(tmp + c, "...", 4u);   /* 含 NUL 结束符 */
    return tmp;
}

void biz_on_accept(q_fd_t fd, uint32_t events, void *ctx)
{
    struct biz_ctx *g = (struct biz_ctx *)ctx;
    q_fd_t      cfd;
    biz_conn_t *c;

    (void)events;
    if (q_net_accept(fd, &cfd) != 0)
        return;   /* 暂无待接受连接（电平触发会再次回调） */

    c = (biz_conn_t *)malloc(sizeof(*c));
    if (!c)
    {
        q_net_close(cfd);
        return;
    }
    c->fd = cfd;
    c->g  = g;
    (void)biz_set_io_timeout(cfd, g_bcfg.io_timeout_sec);

    if (q_tpool_dispatch(g->pool, biz_conn_worker, c) != 0)
    {
        QLOG_WARN(g->log, "线程池队列满，拒绝连接");
        q_net_close(cfd);
        free(c);
    }
}

void biz_conn_worker(void *arg)
{
    biz_conn_t       *c = (biz_conn_t *)arg;
    struct biz_ctx   *g = c->g;
    uint8_t          *buf;
    char              cid[INET6_ADDRSTRLEN + 140];
    char              clipbuf[BIZ_LOG_CAP + 4u];

    buf = (uint8_t *)malloc(BIZ_MAXMSG + 1u);
    if (!buf)
    {
        q_net_close(c->fd);
        free(c);
        return;
    }

    for (;;)
    {
        size_t len = 0u;
        char  *rsp = NULL;

        if (biz_recv_frame(c->fd, buf, BIZ_MAXMSG, &len) != 0)
            break;   /* 对端关闭 / 超时 / 错误 */
        buf[len] = '\0';

        biz_req_cid((const char *)buf, cid, sizeof(cid));
        QLOG_INFO(g->log, "[recv] %s <-- %s",
                  biz_msg_clip((const char *)buf, clipbuf, sizeof(clipbuf)), cid);

        rsp = biz_handle(g, (const char *)buf, len);
        if (!rsp)
        {
            /* 解析失败：回一个 BADREQ，避免 bizd 端读到空响应 */
            static const char e[] =
                "{\"ver\":1,\"id\":\"\",\"result\":2,\"message\":\"bad request\",\"data\":{}}";
            rsp = (char *)malloc(sizeof(e));
            if (rsp)
                memcpy(rsp, e, sizeof(e));
        }
        if (rsp)
        {
            QLOG_INFO(g->log, "[send] %s --> %s",
                      biz_msg_clip(rsp, clipbuf, sizeof(clipbuf)), cid);
            (void)biz_send_frame(c->fd, (const uint8_t *)rsp, strlen(rsp));
            free(rsp);
        }
    }

    free(buf);
    q_net_close(c->fd);
    free(c);
}
