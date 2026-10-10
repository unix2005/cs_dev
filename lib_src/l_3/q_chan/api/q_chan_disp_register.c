/**
 * @file q_chan_disp_register.c
 * @brief 注册 cmd→handler（重复 cmd 覆盖）
 */
#include "headers.h"
#include "q_chan.h"

int q_chan_disp_register(q_chan_disp_t *d, uint16_t cmd, q_chan_handler_t fn, void *ud)
{
    if (!d || !fn)
        return -1;

    /* 重复 cmd 覆盖旧值 */
    for (int i = 0; i < d->n; i++) {
        if (d->cmd[i] == cmd) {
            d->fn[i] = fn;
            d->ud[i] = ud;
            return 0;
        }
    }
    if (d->n >= Q_CHAN_DISP_MAX)
        return -1;

    d->cmd[d->n] = cmd;
    d->fn[d->n] = fn;
    d->ud[d->n] = ud;
    d->n++;
    return 0;
}
