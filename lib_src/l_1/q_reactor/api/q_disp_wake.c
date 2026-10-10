/**
 * @file q_disp_wake.c
 * @brief q_disp_wake —— 向唤醒管道写入 1 字节，唤醒阻塞中的循环
 * @note  写满（EAGAIN）也无妨：管道里已有数据足以唤醒；故一律返回 0。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

int q_disp_wake(q_disp_t *d)
{
    if (!d)
        return -1;
    char b = 1;
    ssize_t n = write(d->wake_w, &b, 1);
    (void)n;
    return 0;
}
