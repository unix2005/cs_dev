/**
 * @file login_srv.c
 * @brief 登录微服务服务器：reactor 监听 accept + 线程池每连接 worker + 定长帧收发
 *
 * 连接模型对齐 bizd 下游用法：bizd 每请求新建一条 TCP 连接，发一帧、等一帧响应后关闭。
 * 监听 socket 为阻塞，故 on_accept 每次仅 accept 一次（epoll 电平触发会再次回调）。
 */
#include "srv_login.h"

/* 日志中单条消息展示的最大长度（超过则截断，避免超长日志行刷屏） */
#define LOGIN_LOG_CAP 2048u

/* user_id 为 64 hex（SM3 摘要），匿名/未登录时全 0；全 0 视为“无用户”，不纳入标识 */
static int login_uid_allzero(const char *s)
{
    if (!s || !*s)
        return 1;
    for (; *s; s++)
        if (*s != '0')
            return 0;
    return 1;
}

/* 从请求 JSON 提取“连接/用户标识”：socket 对端永远是 bizd(127.0.0.1)，无法区分用户，
   真实客户端身份由 bizd 透传在 ctx.src_ip（netd 在 IPC 里填的客户端 IP）。
   格式：<src_ip>#<id>[(uid=<64hex>)]（id 为 bizd 生成的请求号，两端一致便于对账）；
   缺 src_ip 时退化为 chan=<channel_id>#<id>，再退化为 ?#<id> / ?。
   —— 故不用 getpeername，直接读应用层字段。 */
static void login_req_cid(const char *msg, char *out, size_t outsz)
{
    cJSON *root, *ctx, *ip, *id, *chid, *uid;
    const char *ip_s = NULL, *id_s = NULL, *chid_s = NULL, *uid_s = NULL;
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
        ip   = cJSON_GetObjectItem(ctx, "src_ip");
        chid = cJSON_GetObjectItem(ctx, "channel_id");
        uid  = cJSON_GetObjectItem(ctx, "user_id");
        if (ip   && cJSON_IsString(ip)   && ip->valuestring && ip->valuestring[0])
            ip_s = ip->valuestring;
        if (chid && cJSON_IsString(chid) && chid->valuestring && chid->valuestring[0])
            chid_s = chid->valuestring;
        if (uid  && cJSON_IsString(uid)  && uid->valuestring && uid->valuestring[0]
            && !login_uid_allzero(uid->valuestring))
            uid_s = uid->valuestring;
    }
    if (ip_s)
        n = snprintf(out, outsz, "%s#%s", ip_s, id_s ? id_s : "");
    else if (chid_s)
        n = snprintf(out, outsz, "chan=%s#%s", chid_s, id_s ? id_s : "");
    else
        n = snprintf(out, outsz, "?%s%s", id_s ? "#" : "", id_s ? id_s : "");
    if ((size_t)n < outsz && uid_s)
        snprintf(out + n, outsz - (size_t)n, "(uid=%s)", uid_s);
    cJSON_Delete(root);
}

/* 从响应 JSON 提取登录后的鉴权身份（data.user / data.token 前 16 位），用于 [send] 对账；
   非登录成功响应（无 data.user/token，如 BADREQ）则回空串。 */
static void login_resp_ident(const char *msg, char *out, size_t outsz)
{
    cJSON *root, *data, *u, *t;
    const char *us = NULL;
    char        tok[17];
    size_t      L;

    if (outsz == 0u)
        return;
    out[0] = '\0';
    root = cJSON_Parse(msg);
    if (!root)
        return;
    data = cJSON_GetObjectItem(root, "data");
    if (data && cJSON_IsObject(data))
    {
        u = cJSON_GetObjectItem(data, "user");
        t = cJSON_GetObjectItem(data, "token");
        if (u && cJSON_IsString(u))
            us = u->valuestring;
        if (t && cJSON_IsString(t))
        {
            L = strlen(t->valuestring);
            if (L > 16u) L = 16u;
            memcpy(tok, t->valuestring, L);
            tok[L] = '\0';
        }
        else
            tok[0] = '\0';
    }
    if ((us && us[0]) || tok[0])
        snprintf(out, outsz, " (user=%s, tok=%s)", us ? us : "", tok);
    cJSON_Delete(root);
}

/* 把消息裁剪到 LOGIN_LOG_CAP 用于日志展示；超长追加 "..." */
static const char *login_msg_clip(const char *msg, char *tmp, size_t tmpsz)
{
    size_t len = strlen(msg);
    size_t c;
    if (len <= LOGIN_LOG_CAP)
        return msg;
    if (tmpsz < 4u)
        return msg;
    c = (LOGIN_LOG_CAP < tmpsz - 4u) ? LOGIN_LOG_CAP : (tmpsz - 4u);
    memcpy(tmp, msg, c);
    memcpy(tmp + c, "...", 4u);   /* 含 NUL 结束符 */
    return tmp;
}

void login_on_accept(q_fd_t fd, uint32_t events, void *ctx)
{
    struct login_ctx *g = (struct login_ctx *)ctx;
    q_fd_t        cfd;
    login_conn_t *c;

    (void)events;
    if (q_net_accept(fd, &cfd) != 0)
        return;   /* 暂无待接受连接（电平触发会再次回调） */

    {
        /* socket 对端永远是 bizd(127.0.0.1)，不能标识用户；
           真实客户端身份由来帧的 ctx.src_ip 体现，见 worker 日志。 */
        QLOG_INFO(g->log, "接受新连接（等待请求帧）");
    }

    c = (login_conn_t *)malloc(sizeof(*c));
    if (!c)
    {
        q_net_close(cfd);
        return;
    }
    c->fd = cfd;
    c->g  = g;
    (void)login_set_io_timeout(cfd, g_lcfg.io_timeout_sec);

    if (q_tpool_dispatch(g->pool, login_conn_worker, c) != 0)
    {
        QLOG_WARN(g->log, "线程池队列满，拒绝连接");
        q_net_close(cfd);
        free(c);
    }
}

void login_conn_worker(void *arg)
{
    login_conn_t     *c = (login_conn_t *)arg;
    struct login_ctx *g = c->g;
    uint8_t          *buf;
    char              cid[INET6_ADDRSTRLEN + 140];  /* 真实客户端标识：src_ip#id[(uid=<64hex>)] */
    char              clipbuf[LOGIN_LOG_CAP + 4u];

    buf = (uint8_t *)malloc(LOGIN_MAXMSG + 1u);
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
        const char *reply = NULL;
        size_t reply_len = 0u;
        char   auth[64];

        if (login_recv_frame(c->fd, buf, LOGIN_MAXMSG, &len) != 0)
            break;   /* 对端关闭 / 超时 / 错误 */
        buf[len] = '\0';

        login_req_cid((const char *)buf, cid, sizeof(cid));

        QLOG_INFO(g->log, "[recv] %s <-- %s",
                  login_msg_clip((const char *)buf, clipbuf, sizeof(clipbuf)), cid);

        rsp = login_handle(g, (const char *)buf, len);
        if (rsp)
        {
            reply = rsp;
            reply_len = strlen(rsp);
        }
        else
        {
            /* 解析失败：回一个 BADREQ，避免 bizd 端读到空响应 */
            static const char e[] =
                "{\"ver\":1,\"id\":\"\",\"result\":1,\"message\":\"bad request\",\"data\":{}}";
            reply = e;
            reply_len = sizeof(e) - 1u;
        }

        /* 记录回复（在 free(rsp) 之前，避免悬垂指针）：把登录后的鉴权身份附在标识后，再发送 */
        login_resp_ident(reply, auth, sizeof(auth));
        QLOG_INFO(g->log, "[send] %s --> %s%s",
                  login_msg_clip(reply, clipbuf, sizeof(clipbuf)), cid, auth);
        (void)login_send_frame(c->fd, (const uint8_t *)reply, reply_len);
        if (rsp)
            free(rsp);
    }

    free(buf);
    q_net_close(c->fd);
    free(c);
}
