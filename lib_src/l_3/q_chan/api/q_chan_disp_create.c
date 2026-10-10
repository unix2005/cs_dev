/**
 * @file q_chan_disp_create.c
 * @brief 指令分发表创建
 */
#include "headers.h"
#include "q_chan.h"

q_chan_disp_t *q_chan_disp_create(void)
{
    q_chan_disp_t *d = calloc(1, sizeof(*d));
    return d;
}
