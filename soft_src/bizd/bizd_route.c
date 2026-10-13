/**
 * @file bizd_route.c
 * @brief bizd -> 下游微服务的路由选择 + TCP 转发（JSON 信封）
 *
 * 下游微服务由另一项目实现，经 TCP 传输，包格式见 srv_bizd.h 头部注释。
 * 分帧：[4 字节大端长度][JSON]。
 */
#include "srv_bizd.h"

/* cmd -> 下游端点（路由组） */
static const bizd_ep_t *route_of(uint16_t cmd)
{
    switch (cmd)
    {
    case Q_CHAN_CMD_HELLO:
        return &g_bcfg.ep_hello;
    case Q_CHAN_CMD_LOGIN:
    case Q_CHAN_CMD_HEARTBEAT:
    case Q_CHAN_CMD_LOGOUT:
        return &g_bcfg.ep_auth;
    case Q_CHAN_CMD_BIZ_QUERY:
    case Q_CHAN_CMD_BIZ_UPDATE:
        return &g_bcfg.ep_biz;
    case Q_CHAN_CMD_VIDEO_OPEN:
        return &g_bcfg.ep_video;
    default:
        return NULL;
    }
}

int bizd_route_call(uint16_t cmd, const char *req_json, char **rsp_json)
{
    const bizd_ep_t *ep;
    q_fd_t  fd;
    uint8_t *msg = NULL, *rsp = NULL;
    size_t  reqlen, rsplen = 0u;
    int     rc = -1;

    if (!req_json || !rsp_json)
        return -1;
    *rsp_json = NULL;

    ep = route_of(cmd);
    if (!ep || ep->port <= 0)
        return -1;

    if (q_net_connect(ep->host, ep->port, &fd) != 0)
        return -1;

    reqlen = strlen(req_json);
    msg    = (uint8_t *)malloc(Q_IPC_LENPFX + reqlen);
    if (!msg)
        goto out;
    bizd_put_be32(msg, (uint32_t)reqlen);
    memcpy(msg + Q_IPC_LENPFX, req_json, reqlen);

    if (q_net_send_blocking(fd, msg, Q_IPC_LENPFX + reqlen) != (ssize_t)(Q_IPC_LENPFX + reqlen))
        goto out;

    rsp = (uint8_t *)malloc(Q_IPC_MAXMSG + 1u);
    if (!rsp)
        goto out;
    if (bizd_recv_frame(fd, rsp, Q_IPC_MAXMSG, &rsplen) != 0)
    {
        free(rsp);
        rsp = NULL;
        goto out;
    }
    rsp[rsplen] = '\0';
    *rsp_json   = (char *)rsp;
    rsp         = NULL;
    rc          = 0;

out:
    if (msg)
        free(msg);
    if (rsp)
        free(rsp);
    q_net_close(fd);
    return rc;
}
