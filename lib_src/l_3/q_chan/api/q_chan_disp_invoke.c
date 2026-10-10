/**
 * @file q_chan_disp_invoke.c
 * @brief 调用已注册 handler
 */
#include "headers.h"
#include "q_chan.h"

int q_chan_disp_invoke(q_chan_disp_t *d, uint16_t cmd,
                       const uint8_t *body, size_t body_len,
                       uint8_t *out, size_t outcap, size_t *out_len)
{
    if (!d)
        return -1;
    for (int i = 0; i < d->n; i++) {
        if (d->cmd[i] == cmd) {
            if (d->fn[i](cmd, body, body_len, out, outcap, out_len, d->ud[i]) != 0)
                return -1;
            return 0;
        }
    }
    return -1;  /* 未知命令 */
}
