/**
 * @file qc_push_create.c
 * @brief M5：qc_push_create —— 创建推送对象并拷走启动参数
 * @layer  lib_src/l_4/q_clientcomm
 */
#include "qc_push_int.h"   /* 公开 API + 内部结构体 */

#include <stdlib.h>
#include <string.h>

qc_push_t *qc_push_create(const qc_push_cfg_t *cfg)
{
    qc_push_t *p;

    if (!cfg || !cfg->host[0] || cfg->port == 0u || !cfg->user[0])
        return NULL;

    p = (qc_push_t *)calloc(1, sizeof(*p));
    if (!p)
        return NULL;

    memcpy(&p->cfg, cfg, sizeof(p->cfg));
    p->cfg.host[sizeof(p->cfg.host) - 1u] = '\0';
    p->cfg.knock[sizeof(p->cfg.knock) - 1u] = '\0';
    p->cfg.user[sizeof(p->cfg.user) - 1u] = '\0';
    p->cfg.otp[sizeof(p->cfg.otp) - 1u] = '\0';
    p->cfg.cver[sizeof(p->cfg.cver) - 1u] = '\0';
    if (p->cfg.backoff_base_ms == 0u) p->cfg.backoff_base_ms = 1000u;
    if (p->cfg.backoff_max_ms == 0u)  p->cfg.backoff_max_ms = 30000u;

    qc_mutex_init(&p->lock);
    p->lock_inited = 1;
    return p;
}
