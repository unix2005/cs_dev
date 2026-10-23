/**
 * @file qc_conf_validate.c
 * @brief qc_conf_validate —— 业务必填项校验
 *
 * 与 qc_conf_load 分开：加载只负责"把文件变成结构体"，校验独立成一步。
 * 这样 UI 可以先拿到一份可读的配置（比如先在"设置"页展示出来），
 * 再决定怎么提示缺失项，而不是一加载就整体失败、连展示的机会都没有。
 */
#include "qc_client.h"
#include "qc_conf.h"

#include <stdio.h>

int qc_conf_validate(const qc_client_conf_t *c, char *err, size_t err_cap)
{
    if (!c)
        return -1;

    if (err && err_cap > 0u)
        err[0] = '\0';

    if (!c->server.host[0])
    {
        if (err && err_cap > 0u)
            snprintf(err, err_cap, "server.host 未配置（netd 主机地址必填）");
        return 1;
    }

    if (c->server.port == 0u)
    {
        if (err && err_cap > 0u)
            snprintf(err, err_cap, "server.port 非法（须 1..65535，当前 %u）",
                     (unsigned)c->server.port);
        return 2;
    }

    /* 公钥是抗中间人的唯一依据：缺了它，握手变成"跟谁都能握手" */
    if (!c->server.pubkey_path[0])
    {
        if (err && err_cap > 0u)
            snprintf(err, err_cap, "server.pubkey_path 未配置（服务端 SM2 公钥 DER 必填，"
                                   "缺失则无法抗中间人）");
        return 3;
    }

    /* 0 = 用协议默认；显式给了就必须达下限，否则等于自己把抗彩虹表强度降下来 */
    if (c->auth.pbkdf2_iter != 0u && c->auth.pbkdf2_iter < QC_MIN_PBKDF2_ITER)
    {
        if (err && err_cap > 0u)
            snprintf(err, err_cap, "auth.pbkdf2_iter=%u 过低（须 0 或 >= %u）",
                     (unsigned)c->auth.pbkdf2_iter, (unsigned)QC_MIN_PBKDF2_ITER);
        return 4;
    }

    /* 敲门与 SPA 密钥必须成对给：只给一个会在运行期才失败，且表现为"连不上" */
    if (c->knock.endpoint[0] && !c->knock.spa_key_path[0])
    {
        if (err && err_cap > 0u)
            snprintf(err, err_cap, "knock.endpoint 已配置但 knock.spa_key_path 为空"
                                   "（两者必须成对给出）");
        return 5;
    }

    /* M5 推送段：push.knock 与 SPA 密钥同样必须成对 —— 推送栈是独立 knockd 实例，
       只配端点不配密钥，推送连接会在敲门阶段静默失败（表现为"没有实时数据"），
       而这种"没有"与"真的没有数据"在 UI 上无法区分，必须在配置期拦下。 */
    if (c->push.knock[0] && !c->knock.spa_key_path[0])
    {
        if (err && err_cap > 0u)
            snprintf(err, err_cap, "push.knock 已配置但 knock.spa_key_path 为空"
                                   "（推送栈敲门同样需要 SPA 密钥）");
        return 6;
    }

    /* push.port 是 uint16，天然不可能越界；0 = 合法（回退 server.port），无需校验。
       真正要拦的是"配了 push.knock 却没有 SPA 密钥"（上面第 6 项）。 */

    return 0;
}
