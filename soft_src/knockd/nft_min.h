#ifndef NFT_MIN_H
#define NFT_MIN_H
/*
 * 最小化 libnftables C API 声明。
 *
 * 目标机已装运行库 libnftables.so.1（nftables v1.0.2+），但未装 libnftables-dev
 * （缺 nftables/libnftables.h 与 -lnftables 链接名），故在此本地声明稳定公开的
 * 3 个符号，链接期用 -l:libnftables.so.1 直连。符号经 LIBNFTABLES_1 版本标签导出。
 *
 * 【签名血泪史 —— 改前务必读完】
 * 本文件曾把 nft_ctx_new 声明为 (void)、nft_run_cmd_from_buffer 声明为 (ctx, buf)。
 * nftables 实际签名是：
 *     nft_ctx_new(uint32_t flags)                       —— 自 0.9.0 起带 flags
 *     nft_run_cmd_from_buffer(ctx, buf, size_t buflen)  —— 自 1.0.0 起三参数
 * 少传 buflen 时，第三实参取 rdx 里**未初始化的垃圾值**当长度，库内部按其解析
 * 命令缓冲区 → 越界 → 段错误。表现是「knockd 空闲时好好的，一收到合法 SPA 包就
 * 整个进程消失」：即任何能构造/重放合法包的来源都能远程打挂本进程，
 * 而 SPA 又是「静默协议、不回包」，客户端侧完全无感，只剩端口隐身整体失效。
 * （附带：失败形态是"进程消失"而非报错，运维极易误判为配置或权限问题。）
 *
 * 兼容性：三参数调用对旧版(0.9.x 两参数)同样安全 —— 多余的实参被忽略；
 * nft_ctx_new(0) 对老版 (void) 亦然。故统一按**新签名**声明并传全参数。
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>   /* size_t：nft_run_cmd_from_buffer 第三参数类型 */
#include <stdint.h>   /* uint32_t：nft_ctx_new 的 flags 类型 */

struct nft_ctx;

/* 创建/销毁一个 nftables 上下文（内部含 netlink 句柄）。
   flags：0 = 默认行为（旧版无此形参，多传实参被忽略，故恒传 0 即可）。 */
struct nft_ctx *nft_ctx_new(uint32_t flags);
void nft_ctx_free(struct nft_ctx *ctx);

/* 直接把一条 nft 命令送入内核（netlink），不经过 /bin/sh；成功返回 0，失败非 0。
   buflen 必须显式给出（命令串长度，不含结尾 '\0'）：
   1.0.0 起为三参数，漏传会让库把寄存器垃圾当长度而越界崩溃。 */
int nft_run_cmd_from_buffer(struct nft_ctx *ctx, const char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
#endif /* NFT_MIN_H */
