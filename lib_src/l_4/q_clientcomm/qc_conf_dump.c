/**
 * @file qc_conf_dump.c
 * @brief qc_conf_dump —— 打印配置（口令/OTP 脱敏）
 *
 * 为什么要脱敏：调用方通常是 UI 的"诊断/关于"页，内容会被复制进工单、贴到群里。
 * 一次"把配置打出来看看"就把口令泄露出去，是这类工具最典型的翻车方式。
 * 故这里**不提供**任何打印明文口令的开关 —— 想看口令只能去读源文件。
 */
#include "qc_client.h"
#include "qc_conf.h"

#include <stdio.h>

void qc_conf_dump(const qc_client_conf_t *c, void *fp)
{
    FILE *f = (FILE *)fp;

    if (!c)
        return;
    if (!f)
        f = stdout;

    fprintf(f, "--- vms client config ---\n");
    fprintf(f, "  source          : %s\n",
            c->source_path[0] ? c->source_path : "(未加载/默认值)");
    fprintf(f, "  server.host     : %s\n", c->server.host[0] ? c->server.host : "(未配置)");
    fprintf(f, "  server.port     : %u\n", (unsigned)c->server.port);
    fprintf(f, "  server.pubkey   : %s\n",
            c->server.pubkey_path[0] ? c->server.pubkey_path : "(未配置)");
    fprintf(f, "  server.seq0     : %llu\n", (unsigned long long)c->server.seq0);
    fprintf(f, "  server.key_id   : %u\n", (unsigned)c->server.key_id);

    fprintf(f, "  auth.user       : %s\n", c->auth.user[0] ? c->auth.user : "(未配置)");
    fprintf(f, "  auth.password   : %s\n",
            c->auth.password[0] ? "****（已配置，不打印）" : "(未配置)");
    fprintf(f, "  auth.otp        : %s\n",
            c->auth.otp[0] ? "****（已配置，不打印）" : "(未配置)");
    fprintf(f, "  auth.devid_hex  : %s\n",
            c->auth.devid_hex[0] ? c->auth.devid_hex : "(用内置默认)");
    fprintf(f, "  auth.pbkdf2_iter: %u%s\n", (unsigned)c->auth.pbkdf2_iter,
            c->auth.pbkdf2_iter == 0u ? "（0 = 协议默认）" : "");

    fprintf(f, "  knock.endpoint  : %s\n",
            c->knock.endpoint[0] ? c->knock.endpoint : "(不敲门)");
    fprintf(f, "  knock.spa_key   : %s\n",
            c->knock.spa_key_path[0] ? c->knock.spa_key_path : "(无)");
    fprintf(f, "  knock.req_port  : %u\n", (unsigned)c->knock.req_port);
    fprintf(f, "  knock.req_ttl   : %us\n", (unsigned)c->knock.req_ttl);
    fprintf(f, "  knock.key_id    : %u\n", (unsigned)c->knock.key_id);

    fprintf(f, "  keeper.heartbeat: %ums\n", (unsigned)c->keeper.heartbeat_ms);
    fprintf(f, "  keeper.backoff  : %ums .. %ums\n",
            (unsigned)c->keeper.backoff_base_ms, (unsigned)c->keeper.backoff_max_ms);
    fprintf(f, "  keeper.max_retry: %u%s\n", (unsigned)c->keeper.max_retry,
            c->keeper.max_retry == 0u ? "（0 = 无限重试）" : "");
    fprintf(f, "  keeper.auto     : %u\n", (unsigned)c->keeper.auto_start);

    fprintf(f, "  app.cver        : %s\n", c->app.client_version);
    fprintf(f, "  app.log_level   : %s\n", c->app.log_level);
    fprintf(f, "  app.conn_timeout: %ums\n", (unsigned)c->app.connect_timeout_ms);

    fprintf(f, "  push.host       : %s\n",
            c->push.host[0] ? c->push.host : "(未配置，回退 server.host)");
    fprintf(f, "  push.port       : %u%s\n", (unsigned)c->push.port,
            c->push.port == 0u ? "（0 = 回退 server.port）" : "");
    fprintf(f, "  push.knock      : %s\n",
            c->push.knock[0] ? c->push.knock : "(未配置，回退 knock.endpoint)");

    fprintf(f, "  flags           : 0x%04x%s%s\n", (unsigned)c->flags,
            (c->flags & QC_CONF_F_PLAIN_PASSWORD) ? " [明文口令]" : "",
            (c->flags & QC_CONF_F_DEFAULTED) ? " [关键项走了默认值]" : "");
    fprintf(f, "-------------------------\n");
}
