/**
 * @file qc_login_ut.c
 * @brief 登录闭环端到端联调程序（三端通用：Windows / Linux / macOS）
 * @layer  lib_src/l_4/q_clientcomm/ut  ->  仅链接 libqclientcomm 及其闭包
 *
 * 覆盖链路：
 *   [可选 SPA 敲门 UDP] -> TCP 连接 + SM2 握手 -> HELLO -> SALT_QUERY
 *   -> PBKDF2-SM3 本地派生 -> LOGIN(取 32B Token) -> HEARTBEAT
 *   -> [M2] BIZ_QUERY(biz_type) -> LOGOUT -> 关闭
 *
 * 定位：这是 M1「打通登录闭环」+ M2「打通一条业务查询」的验收程序，
 *       需要**真实运行中的 netd/bizd/login[/biz]**；
 *       纯本地（不触网）的冒烟测试见同目录 qc_ut.c（make ut）。
 *
 * 编译（Linux/macOS）：make -C lib_src/l_4/q_clientcomm ut_login
 * 编译（Windows）  ：shell/build_login_ut_win.sh
 *
 * 用法：
 *   qc_login_ut --host <ip> --port <n> --user <name> --pass <pwd> --pub <server_pub.der>
 *               [--otp <code>] [--iter <n>] [--devid <hex>] [--cver <str>]
 *               [--knock <ip:port>] [--spakey <file>] [--reqport <n>] [--reqttl <n>]
 *               [--biz-type <type>] [--biz-param <json>] [--no-biz] [--keep] [--v]
 *   --pub      服务端静态 SM2 公钥 DER（netd 的 pub_file），**必填**（抗中间人）
 *   --knock    knockd 的 UDP 地址；未给出则跳过敲门（服务端未部署端口隐身时）
 *   --spakey   SPA 密钥文件（16 字节），随 --knock 使用
 *   --reqport  敲门申请放行的端口（通常等于 --port）
 *   其余参数含义见下方 usage()。
 */
#include "qc_client.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "include_ipc_proto.h" /* Q_PBKDF2_ITER_DEFAULT / Q_IPC_T_BODY */
#include "q_codec.h"          /* q_codec_tlv_decode：查看业务响应 BODY（仅打印，不解析 JSON） */

/* ------------------------------ 参数 ------------------------------ */
typedef struct
{
    const char *host;
    int         port;
    const char *user;
    const char *pass;
    const char *pub;          /* 服务端 SM2 公钥 DER 路径 */
    const char *otp;
    uint32_t    iter;
    const char *devid_hex;    /* 设备指纹 hex（缺省内置固定值） */
    const char *cver;
    const char *knock;        /* "ip:port"，NULL=不敲门 */
    const char *spakey;       /* SPA 密钥文件 */
    int         reqport;
    int         reqttl;
    /* —— M2：业务查询 —— */
    const char *biz_type;     /* 默认 "stats" */
    const char *biz_param;    /* 业务参数 JSON，可 NULL */
    uint32_t    biz_offset;   /* 分页偏移（M3：列表类业务用） */
    uint32_t    biz_limit;    /* 每页条数；0=由服务端取默认 */
    int         no_biz;       /* 1=跳过业务查询（只验登录） */
    int         keep;         /* 1=登录后不断开（供手工观察），跳过 logout */
    int         verbose;
} args_t;

static void usage(const char *argv0)
{
    fprintf(stderr,
        "用法: %s --host <ip> --port <n> --user <name> --pass <pwd> --pub <der>\n"
        "        [--otp <code>] [--iter <n>] [--devid <hex>] [--cver <str>]\n"
        "        [--knock <ip:port>] [--spakey <file>] [--reqport <n>] [--reqttl <n>]\n"
        "        [--biz-type <type>] [--biz-param <json>] [--offset <n>] [--limit <n>]\n"
        "        [--no-biz] [--keep] [--v]\n"
        "  --pub     服务端静态 SM2 公钥 DER（netd pub_file），必填\n"
        "  --knock   knockd UDP 地址（如 127.0.0.1:5001）；不给则跳过 SPA 敲门\n"
        "  --spakey  SPA 密钥文件（16 字节）；配合 --knock，缺省读环境变量 GTKVMS_SPA_KEY_FILE\n"
        "  --iter    PBKDF2-SM3 迭代次数（默认 %u，须与登录服务一致）\n"
        "  --biz-type  业务子类型（默认 stats）；服务端未实现时回结果码 6(UNSUPPORTED)\n"
    "  --biz-param 业务参数（UTF-8 JSON 文本），随 BIZ_QUERY 下发\n"
    "  --offset/--limit  分页（M3：companies / vehicles 列表类业务）；limit 默认由服务端取\n"
    "  --no-biz    跳过业务查询，只验登录闭环\n"
        "  --keep    登录成功后保持连接不登出（用于观察服务端日志）\n",
        argv0, (unsigned)Q_PBKDF2_ITER_DEFAULT);
}

/* ------------------------------ 小工具 ------------------------------ */
static int g_fail = 0;

static void step(int ok, const char *fmt, ...)
{
    va_list ap;
    printf("  [%s] ", ok ? "OK  " : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (!ok)
        g_fail++;
}

/* 读整个文件到新分配缓冲；失败返回 NULL 并打印原因 */
static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE   *f;
    uint8_t *buf = NULL;
    long     sz;

    if (!path)
        return NULL;
    f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "  打开文件失败: %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0)
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)sz + 1u);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
    {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex -> 字节；返回字节数，失败 -1 */
static int from_hex(const char *hex, uint8_t *out, size_t outcap)
{
    size_t i, n;

    if (!hex)
        return -1;
    n = strlen(hex) / 2u;
    if (n == 0u || n > outcap || (strlen(hex) % 2u) != 0u)
        return -1;
    for (i = 0u; i < n; i++)
    {
        int hi = hexval(hex[2u * i]);
        int lo = hexval(hex[2u * i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

static void to_hex(const uint8_t *b, size_t len, char *out, size_t outcap)
{
    static const char *d = "0123456789abcdef";
    size_t i;

    if (outcap < len * 2u + 1u)
        return;
    for (i = 0u; i < len; i++)
    {
        out[i * 2u]     = d[(b[i] >> 4) & 0x0fu];
        out[i * 2u + 1] = d[b[i] & 0x0fu];
    }
    out[len * 2u] = '\0';
}

/* ------------------------------ 参数解析 ------------------------------ */
static int parse_args(int argc, char **argv, args_t *a)
{
    int i;

    memset(a, 0, sizeof(*a));
    a->port    = 18099;
    a->iter    = Q_PBKDF2_ITER_DEFAULT;
    a->otp     = "000000";
    a->cver    = "qc_login_ut/1.0";
    a->devid_hex = "51565f55545f30303031";   /* "QV_UT_0001" */
    a->reqttl  = 300;
    a->biz_type = "stats";

    for (i = 1; i < argc; i++)
    {
        const char *k = argv[i];
        if (i + 1 >= argc && strcmp(k, "--keep") != 0 && strcmp(k, "--v") != 0
            && strcmp(k, "--no-biz") != 0)
        {
            fprintf(stderr, "参数 %s 缺值\n", k);
            return -1;
        }
        if      (!strcmp(k, "--host"))    a->host    = argv[++i];
        else if (!strcmp(k, "--port"))    a->port    = atoi(argv[++i]);
        else if (!strcmp(k, "--user"))    a->user    = argv[++i];
        else if (!strcmp(k, "--pass"))    a->pass    = argv[++i];
        else if (!strcmp(k, "--pub"))     a->pub     = argv[++i];
        else if (!strcmp(k, "--otp"))     a->otp     = argv[++i];
        else if (!strcmp(k, "--iter"))    a->iter    = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(k, "--devid"))   a->devid_hex = argv[++i];
        else if (!strcmp(k, "--cver"))    a->cver    = argv[++i];
        else if (!strcmp(k, "--knock"))   a->knock   = argv[++i];
        else if (!strcmp(k, "--spakey"))  a->spakey  = argv[++i];
        else if (!strcmp(k, "--reqport")) a->reqport = atoi(argv[++i]);
        else if (!strcmp(k, "--reqttl"))  a->reqttl  = atoi(argv[++i]);
        else if (!strcmp(k, "--biz-type"))  a->biz_type  = argv[++i];
        else if (!strcmp(k, "--biz-param")) a->biz_param = argv[++i];
        else if (!strcmp(k, "--offset"))    a->biz_offset = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(k, "--limit"))     a->biz_limit  = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(k, "--no-biz"))    a->no_biz    = 1;
        else if (!strcmp(k, "--keep"))    a->keep    = 1;
        else if (!strcmp(k, "--v"))       a->verbose = 1;
        else
        {
            fprintf(stderr, "未知参数: %s\n", k);
            return -1;
        }
    }

    if (!a->host || !a->user || !a->pass || !a->pub)
    {
        usage(argv[0]);
        return -1;
    }
    if (a->reqport == 0)
        a->reqport = a->port;
    return 0;
}

/* "ip:port" -> 拆成 host 缓冲与端口 */
static int split_ep(const char *s, char *host, size_t hostcap, int *port)
{
    const char *p;

    if (!s || !host || !port)
        return -1;
    p = strrchr(s, ':');
    if (!p || p == s)
        return -1;
    if ((size_t)(p - s) >= hostcap)
        return -1;
    memcpy(host, s, (size_t)(p - s));
    host[p - s] = '\0';
    *port = atoi(p + 1);
    return (*port > 0) ? 0 : -1;
}

/* ------------------------------ 主流程 ------------------------------ */
int main(int argc, char **argv)
{
    args_t        a;
    uint8_t      *pub = NULL;
    size_t        pub_len = 0u;
    qc_session_t *s = NULL;
    uint8_t       salt[64];
    uint8_t       pwd_hash[32];
    uint8_t       token[32];
    uint8_t       devid[32];
    size_t        salt_len = 0u, devid_len = 0u;
    char          hex[128];
    int           rc;
    clock_t       t0, t1;

    if (parse_args(argc, argv, &a) != 0)
        return 2;

    printf("== qc_login_ut: 登录闭环联调 ==\n");
    printf("  目标 %s:%d  用户 %s  迭代 %u  OTP %s\n",
           a.host, a.port, a.user, (unsigned)a.iter, a.otp);

    /* 1. 服务端公钥 */
    pub = read_file(a.pub, &pub_len);
    if (!pub || pub_len == 0u)
    {
        fprintf(stderr, "读取服务端公钥失败: %s\n", a.pub);
        free(pub);
        return 2;
    }
    step(1, "载入服务端 SM2 公钥 DER: %s (%u 字节)", a.pub, (unsigned)pub_len);

    /* 2. 设备指纹 */
    {
        int n = from_hex(a.devid_hex, devid, sizeof(devid));
        if (n <= 0)
        {
            fprintf(stderr, "devid hex 非法: %s\n", a.devid_hex);
            free(pub);
            return 2;
        }
        devid_len = (size_t)n;
    }

    /* 3. SPA 敲门（可选） */
    if (a.knock)
    {
        char  khost[64];
        int   kport = 0;
        const char *keypath = a.spakey ? a.spakey : getenv("GTKVMS_SPA_KEY_FILE");
        uint8_t key[32];
        size_t  keylen = 0u;
        uint8_t user_id[32];
        size_t  ulen;

        if (split_ep(a.knock, khost, sizeof(khost), &kport) != 0)
        {
            fprintf(stderr, "--knock 格式应为 ip:port: %s\n", a.knock);
            free(pub);
            return 2;
        }
        if (!keypath)
        {
            fprintf(stderr, "敲门需要 SPA 密钥：用 --spakey 或环境变量 GTKVMS_SPA_KEY_FILE\n");
            free(pub);
            return 2;
        }
        {
            uint8_t *kb = read_file(keypath, &keylen);
            if (!kb || keylen == 0u || keylen > sizeof(key))
            {
                fprintf(stderr, "SPA 密钥读取失败或长度非法(需16字节): %s\n", keypath);
                free(kb);
                free(pub);
                return 2;
            }
            memcpy(key, kb, keylen);
            memset(kb, 0, keylen);
            free(kb);
        }
        memset(user_id, 0, sizeof(user_id));
        ulen = strlen(a.user);
        if (ulen > sizeof(user_id)) ulen = sizeof(user_id);
        memcpy(user_id, a.user, ulen);

        t0 = clock();
        rc = qc_knock(khost, kport, key, keylen, 0, user_id,
                      (uint16_t)a.reqport, (uint16_t)a.reqttl);
        t1 = clock();
        memset(key, 0, sizeof(key));
        step(rc == 0, "SPA 敲门 -> %s:%d (req_port=%d ttl=%d) %.0fms",
             khost, kport, a.reqport, a.reqttl,
             (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC);
        if (rc != 0)
        {
            free(pub);
            return 1;
        }
    }
    else
    {
        printf("  [--] 未指定 --knock，跳过 SPA 敲门\n");
    }

    /* 4. 会话 + 连接 + 握手 */
    s = qc_session_create(pub, pub_len, 1, 1);
    free(pub);
    if (!s)
    {
        step(0, "qc_session_create");
        return 1;
    }
    step(1, "qc_session_create");

    t0 = clock();
    rc = qc_connect(s, a.host, a.port);
    t1 = clock();
    step(rc == 0, "qc_connect + SM2 握手 %.0fms",
         (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC);
    if (rc != 0)
    {
        qc_session_destroy(s);
        return 1;
    }

    /* 5. HELLO */
    rc = qc_hello(s);
    step(rc == 0, "qc_hello rc=%d", rc);
    if (rc != 0)
    {
        qc_session_destroy(s);
        return 1;
    }

    /* 6. SALT_QUERY */
    memset(salt, 0, sizeof(salt));
    rc = qc_fetch_salt(s, a.user, salt, sizeof(salt), &salt_len);
    if (rc == 0)
    {
        to_hex(salt, salt_len, hex, sizeof(hex));
        step(1, "qc_fetch_salt -> %u 字节 salt=%s", (unsigned)salt_len, hex);
    }
    else
    {
        step(0, "qc_fetch_salt rc=%d", rc);
        qc_session_destroy(s);
        return 1;
    }

    /* 7. 本地派生 pwd_hash */
    t0 = clock();
    rc = qc_pbkdf2_sm3((const uint8_t *)a.pass, strlen(a.pass),
                       salt, salt_len, a.iter, pwd_hash);
    t1 = clock();
    if (rc == 0)
    {
        to_hex(pwd_hash, 32u, hex, sizeof(hex));
        step(1, "qc_pbkdf2_sm3(iter=%u) -> %s  %.0fms",
             (unsigned)a.iter, hex,
             (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC);
        memset(hex, 0, sizeof(hex));
    }
    else
    {
        step(0, "qc_pbkdf2_sm3");
        qc_session_destroy(s);
        return 1;
    }

    /* 8. LOGIN */
    t0 = clock();
    rc = qc_login(s, a.user, pwd_hash, a.otp, devid, devid_len, a.cver, token);
    t1 = clock();
    memset(pwd_hash, 0, sizeof(pwd_hash));   /* 派生值不滞留 */
    if (rc == 0)
    {
        to_hex(token, 32u, hex, sizeof(hex));
        step(1, "qc_login -> Token(32B)=%s  %.0fms", hex,
             (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC);
    }
    else
    {
        step(0, "qc_login rc=%d（0=成功，>0=结果码，<0=断连）", rc);
        qc_session_destroy(s);
        return 1;
    }

    /* 9. HEARTBEAT */
    rc = qc_heartbeat(s);
    step(rc == 0, "qc_heartbeat rc=%d", rc);

    /* 9.5 业务查询（M2：BIZ_QUERY + biz_type）。
       客户端约定 TLV-only：这里只把下游 JSON 原文打印出来供对账，不做解析。 */
    if (!a.no_biz)
    {
        /* 列表类业务（companies/vehicles）一页 JSON 可达数十 KB，
           缓冲必须大于 Q_IPC_TLV_MAX(60000)，否则门面会直接报"缓冲不足"。 */
        uint8_t resp[65536];
        size_t  rlen = 0u;

        t0 = clock();
        rc = qc_biz_query(s, a.biz_type, a.biz_param, a.biz_offset, a.biz_limit,
                          resp, sizeof(resp), &rlen);
        t1 = clock();
        step(rc == 0, "qc_biz_query(type=%s, param=%s, offset=%u, limit=%u) rc=%d  %.0fms",
             a.biz_type, a.biz_param ? a.biz_param : "(none)",
             (unsigned)a.biz_offset, (unsigned)a.biz_limit, rc,
             (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC);

        if (rc == 0)
        {
            size_t   off = 0u;
            uint16_t blen = 0u;
            const uint8_t *body = NULL;

            while (off < rlen)
            {
                uint16_t tag = 0u, vlen = 0u;
                const uint8_t *val = NULL;
                size_t used = 0u;

                if (q_codec_tlv_decode(resp + off, rlen - off, &tag, &val, &vlen, &used) != 0)
                    break;
                off += used;
                if (tag == Q_IPC_T_BODY)
                {
                    body = val;
                    blen = vlen;
                }
            }
            if (body && blen > 0u)
            {
                /* 列表响应可达几十 KB，完整打印会淹没终端：超过 1500 字节只打头尾 */
                printf("       下游响应(%u 字节): ", (unsigned)blen);
                if (blen <= 1500u)
                {
                    printf("%.*s\n", (int)blen, (const char *)body);
                }
                else
                {
                    printf("%.1200s ...[省略 %u 字节]... %.200s\n",
                           (const char *)body, (unsigned)(blen - 1400u),
                           (const char *)body + (blen - 200u));
                }
            }
            else
                step(0, "业务响应缺少 BODY");
        }
    }
    else
    {
        printf("  [--] --no-biz：跳过业务查询\n");
    }

    /* 10. LOGOUT（--keep 时跳过） */
    if (!a.keep)
    {
        rc = qc_logout(s);
        step(rc == 0, "qc_logout rc=%d", rc);
    }
    else
    {
        printf("  [--] --keep：保持连接，15s 后退出\n");
        {
            clock_t w0 = clock();
            while ((double)(clock() - w0) / (double)CLOCKS_PER_SEC < 15.0)
                ;
        }
    }

    qc_session_destroy(s);
    memset(token, 0, sizeof(token));

    printf("\n%s\n", g_fail == 0 ? "[LOGIN CLOSED-LOOP ALL PASS]" : "[SOME FAILED]");
    return g_fail == 0 ? 0 : 1;
}
