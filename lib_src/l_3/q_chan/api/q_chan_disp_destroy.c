/**
 * @file q_chan_disp_destroy.c
 * @brief 指令分发表销毁
 */
#include "headers.h"
#include "q_chan.h"

void q_chan_disp_destroy(q_chan_disp_t *d)
{
    free(d);
}
