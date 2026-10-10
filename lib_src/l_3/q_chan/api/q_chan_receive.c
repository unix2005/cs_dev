/**
 * @file q_chan_receive.c
 * @brief q_chan_receive —— 经 fd 阻塞读取完整包并解密
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

int q_chan_receive(q_chan_t *c, int fd, uint8_t *plain, size_t plaincap, size_t *plainlen, uint16_t *cmd_out)
{
    if (!c || !c->established)
        return -1;
    uint8_t hdr[Q_CODEC_HDR_MIN];
    if (q_net_recv_blocking(fd, hdr, Q_CODEC_HDR_MIN) != Q_CODEC_HDR_MIN)
        return -1;

    q_codec_pkt_hdr_t h;
    if (q_codec_pkt_hdr_unpack(hdr, Q_CODEC_HDR_MIN, &h) != 0)
        return -1;
    if (h.hdr_len != Q_CODEC_HDR_MIN)
        return -1;
    size_t need = (size_t)h.hdr_len + (size_t)h.body_len + Q_CHAN_TAG_LEN;

    uint8_t *buf = malloc(need);
    if (!buf)
        return -1;
    memcpy(buf, hdr, Q_CODEC_HDR_MIN);
    if (q_net_recv_blocking(fd, buf + Q_CODEC_HDR_MIN, need - Q_CODEC_HDR_MIN) != (ssize_t)(need - Q_CODEC_HDR_MIN))
    {
        free(buf);
        return -1;
    }
    int rc = q_chan_recv(c, buf, need, plain, plaincap, plainlen, cmd_out);
    free(buf);
    return rc;
}
