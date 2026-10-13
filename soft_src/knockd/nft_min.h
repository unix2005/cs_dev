#ifndef NFT_MIN_H
#define NFT_MIN_H
/*
 * 最小化 libnftables C API 声明。
 *
 * 目标机已装运行库 libq... 实为系统 libnftables.so.1（nftables v1.0.9），
 * 但未装 libnftables-dev（缺 nftables/libnftables.h 与 libq... 链接名），
 * 故在此本地声明稳定公开的 3 个符号，链接期用 -l:libnftables.so.1 直连。
 * 以下 API 自 nftables 0.9 起稳定，符号经 LIBNFTABLES_1 版本标签导出。
 */
#ifdef __cplusplus
extern "C" {
#endif

struct nft_ctx;

/* 创建/销毁一个 nftables 上下文（内部含 netlink 句柄）；参数为 void（旧 cfg 形参已废弃） */
struct nft_ctx *nft_ctx_new(void);
void nft_ctx_free(struct nft_ctx *ctx);

/* 直接把一条 nft 命令送入内核（netlink），不经过 /bin/sh；成功返回 0，失败非 0 */
int nft_run_cmd_from_buffer(struct nft_ctx *ctx, char *buf);

#ifdef __cplusplus
}
#endif
#endif /* NFT_MIN_H */
