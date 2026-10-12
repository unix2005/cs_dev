# ==============================================
#  lib_src/l_1/q_crypto  ——  l_1 基础层 国密原语封装库（libqcrypto）
#  首行必须引入公共配置
#  目录结构（见 AI_SKILL_RULE §3.1）：
#    headers.h      聚合公共基础头 + Tongsuo 头（仅模块内 api/ 引用，不安装）
#    q_crypto.h     对外头文件（自包含，外部可直接 #include <q_crypto.h>）
#    Makefile
#    api/           C 源代码（q_crypto_*.c，每函数一文件）
#    ut/            单元测试 / 小程序（q_crypto_ut.c）
#  源文件引用铁律：api/ 下每个 .c 仅 #include "headers.h" 与 "q_crypto.h"
#
#  第三方依赖（唯一允许直接链接 Tongsuo 的模块，AI_SKILL_RULE §3.3 第5条）：
#    Makefile.inc 的 EXT_LIB 指向 tongsuo/lib（实际不存在，库在 tongsuo/lib64），
#    且 lib_so 链接命令未带 Tongsuo 的 -L；故此处显式补充 -L 与运行时 rpath，
#    确保 libqcrypto.so 既能链接也能在加载时找到 libcrypto/libssl。
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqcrypto.a
TARGET_SO = libqcrypto.so
OBJS   = api/q_crypto_sm3.o api/q_crypto_sm3_kdf.o \
         api/q_crypto_hmac_sm3.o api/q_crypto_pbkdf2_sm3.o \
         api/q_crypto_sm4_gcm_encrypt.o api/q_crypto_sm4_gcm_decrypt.o \
         api/q_crypto_sm2_keygen.o api/q_crypto_sm2_sign.o \
         api/q_crypto_sm2_verify.o api/q_crypto_sm2_derive.o \
         api/q_crypto_totp_sm3.o

include ../../../include/Makefile.inc

# 唯一允许直接链接 Tongsuo：补充库路径 + 运行时 rpath（注意 LIB_CRYPTO 在 include 之后定义）
LIBS += $(SOFT_HOME)/tongsuo/lib64 

# 对外头文件归集到 include/
install_hdr:
	cp -f q_crypto.h $(INCLUDE)/q_crypto.h

# 扩展默认 all：仅追加先决条件（无配方体，不触发"覆盖配方"告警）
all: lib_static lib_so install_lib install_hdr

# 单元测试：构建并运行 ut/ 小程序（链接 libqcrypto.so，rpath 指向 lib/）
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_crypto_ut.c -o ut/bin/q_crypto_ut -L$(LIB) -L$(LIBS) -lqcrypto -lpthread $(LIB_CRYPTO) -Wl,-rpath,$(LIB) -Wl,-rpath,$(LIBS)
	./ut/bin/q_crypto_ut

.PHONY: install_hdr ut
