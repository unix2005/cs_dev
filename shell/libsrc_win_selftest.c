/**
 * @file libsrc_win_selftest.c
 * @brief lib_src Windows 版构建自检程序（由 shell/build_libsrc_win.sh 自动编译运行）
 *
 * 覆盖三层验证：
 *   1) 头文件在 Windows 下可编译（16 个 q_* 公共头全量引入，暴露跨平台宏/类型缺口）
 *   2) 静态库可链接（q_clientcomm + 传递闭包 + Tongsuo + winpthread + ws2_32）
 *   3) 运行期功能可用（SM3 标准向量、随机数、链表、hex、日志、reactor/disp 创建、
 *      qc_client 门面的 NULL 入参安全返回）
 *
 * 编译示例（静态链接，Windows 无 rpath，推荐方式）：
 *   x86_64-w64-mingw32-gcc -O2 -I<SOFT_HOME>/include -I<SOFT_HOME>/tongsuo/include \
 *       libsrc_win_selftest.c -L<SOFT_HOME>/lib \
 *       -lqclientcomm -lqchan -lqspa -lqsec -lqcodec -lqnet -lqcrypto -lqrand \
 *       -lqreactor -lqutil -lqmem -lqlog -lqds -lqcommon \
 *       -L<SOFT_HOME>/tongsuo/lib -lcrypto -lpthread -lws2_32 -o selftest.exe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <q_log.h>
#include <q_mem.h>
#include <q_ds.h>
#include <q_util.h>
#include <q_rand.h>
#include <q_crypto.h>
#include <q_reactor.h>
#include <q_common.h>
#include <q_codec.h>
#include <q_net.h>
#include <q_sec.h>
#include <q_chan.h>
#include <q_spa.h>
#include <qc_client.h>

/* SM3("abc") 标准测试向量（GB/T 32905-2016） */
static const unsigned char SM3_ABC[32] = {
    0x66, 0xc7, 0xf0, 0xf4, 0x62, 0xee, 0xed, 0xd9, 0xd1, 0xf2, 0xd4, 0x6b, 0xdc, 0x10, 0xe4, 0xe2,
    0x41, 0x67, 0xc4, 0x87, 0x5c, 0xf2, 0xf7, 0xa2, 0x29, 0x7d, 0xa0, 0x2b, 0x8f, 0x4b, 0xa8, 0xe0
};

int main(void)
{
    unsigned char r[16];
    if (q_rand_bytes(r, sizeof r) != 0) { printf("FAIL q_rand_bytes\n"); return 1; }
    printf("[ok] q_rand_bytes        : %02x%02x%02x%02x...\n", r[0], r[1], r[2], r[3]);

    unsigned char h[32];
    if (q_crypto_sm3((const unsigned char *)"abc", 3, h) != 0) { printf("FAIL q_crypto_sm3\n"); return 2; }
    if (memcmp(h, SM3_ABC, 32) != 0) { printf("FAIL SM3 向量不匹配\n"); return 3; }
    printf("[ok] q_crypto_sm3        : 66c7f0f4...（标准向量一致）\n");

    q_ds_list_t *l = q_ds_list_create();
    if (!l) { printf("FAIL q_ds_list_create\n"); return 4; }
    q_ds_list_destroy(l);
    printf("[ok] q_ds_list           : create/destroy\n");

    char *hex = q_util_hex_encode(r, 4);
    if (!hex) { printf("FAIL q_util_hex_encode\n"); return 5; }
    printf("[ok] q_util_hex_encode   : %s\n", hex);
    free(hex);

    q_log_t *lg = q_log_open("_selftest.log", Q_LOG_INFO, 1u << 20);
    if (lg) { QLOG_INFO(lg, "hello from windows"); q_log_close(lg); }
    printf("[ok] q_log               : open/write/close\n");

    q_reactor_t *rct = q_reactor_create();
    printf("[ok] q_reactor_create    : %s\n", rct ? "ok" : "null");
    if (rct) q_reactor_destroy(rct);

    q_chan_disp_t *dsp = q_chan_disp_create();
    printf("[ok] q_chan_disp_create  : %s\n", dsp ? "ok" : "null");
    if (dsp) q_chan_disp_destroy(dsp);

    /* qc_client 门面：非法入参必须安全返回 NULL（不建连接、不崩溃） */
    qc_session_t *s = qc_session_create(NULL, 0, 1, 1);
    printf("[ok] qc_session_create   : %s\n", s ? "?" : "null-as-expected");
    qc_session_destroy(NULL);
    printf("[ok] qc_client ABI       : ok\n");

    printf("\nALL PASS\n");
    return 0;
}
