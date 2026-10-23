/**
 * @file qc_push_start.c
 * @brief M5：qc_push_start —— 启动推送长连接（阻塞建连 + 拉起接收线程）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 调用方须放后台线程：内部含 knock/握手/登录等多个网络往返。
 * password 是瞬态口令：只在本次建连的 PBKDF2 派生中使用，函数返回后栈上即失效，
 * 本模块不保存任何形式的凭据。
 */
#include "qc_push_int.h"   /* 公开 API + 内部结构体 */

#include <string.h>

int qc_push_start(qc_push_t *p, const char *password)
{
    int rc;

    if (!p)
        return -1;
    if (!password || !password[0])
        return -1;               /* 初始建连必须有口令（重连才走 on_relogin） */

    /* 已在跑：先停干净再起，避免两个接收线程抢同一条连接 */
    if (p->thread_on)
        (void)qc_push_stop(p);

    p->stop = 0;
    rc = qc_push_connect(p, password);
    if (rc != 0)
        return (rc > 0) ? rc : -1;

    if (qc_thread_start(&p->thread, qc_push_thread, p) != 0)
    {
        /* 线程起不来：连接已建立但没人收 —— 关掉，避免半开状态 */
        p->connected = 0;
        qc_close(p->sess);
        return -1;
    }
    p->thread_on = 1;
    return 0;
}
