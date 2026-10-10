/**
 * @file q_chan_transmit.c
 * @brief q_chan_transmit —— 经 fd 阻塞发送编码后的数据包
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

int q_chan_transmit(q_chan_t *c, int fd, const uint8_t *plain, size_t plen, uint16_t cmd)
{
    if (!c || !c->established)
        return -1;
    uint8_t *out = malloc((size_t)Q_CODEC_HDR_MIN + plen + Q_CHAN_TAG_LEN);
    if (!out)
        return -1;
    size_t outlen = 0;
    if (q_chan_send(c, plain, plen, cmd, out, (size_t)Q_CODEC_HDR_MIN + plen + Q_CHAN_TAG_LEN, &outlen) != 0)
    {
        free(out);
        return -1;
    }
    ssize_t n = q_net_send_blocking(fd, out, outlen);
    free(out);
    return (size_t)n == outlen ? 0 : -1;
}
