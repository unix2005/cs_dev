/**
 * @file qc_push_destroy.c
 * @brief M5：qc_push_destroy —— 停止线程并整体释放（幂等）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 顺序不能反：必须先停接收线程（它持有 sess 的引用），再毁会话，
 * 否则线程会摸到已释放的 chan/fd。与 qc_session_destroy 停 keeper 同理。
 */
#include "qc_push_int.h"   /* 公开 API + 内部结构体 */

#include <stdlib.h>
#include <string.h>

void qc_push_destroy(qc_push_t *p)
{
    if (!p)
        return;

    (void)qc_push_stop(p);

    if (p->sess)
    {
        qc_session_destroy(p->sess);   /* 内部会停未起的 keeper（无害）并清零 token */
        p->sess = NULL;
    }
    if (p->vids)
    {
        free(p->vids);
        p->vids = NULL;
    }
    p->nvid = 0u;
    p->cap_vid = 0u;
    if (p->lock_inited)
    {
        qc_mutex_fini(&p->lock);
        p->lock_inited = 0;
    }
    /* cfg 里含 SPA 密钥，整体归零再释放 —— 密钥不留在已释放的堆块里 */
    memset(p, 0, sizeof(*p));
    free(p);
}
