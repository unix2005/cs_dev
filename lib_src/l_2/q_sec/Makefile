# ==============================================
#  lib_src/l_2/q_sec  ——  l_2 能力层 安全能力库（libqsec）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：密钥派生(SM3-KDF) / 防重放(seq 滑动窗口) / PoW / 限流计数器
#  依赖：l_1（q_crypto 国密原语、q_mem 安全清零、q_util、q_log、q_rand）
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqsec.a
TARGET_SO = libqsec.so
OBJS   = api/q_sec_kdf_derive.o \
         api/q_sec_session_key.o \
         api/q_sec_seq_window.o \
         api/q_sec_pow.o \
         api/q_sec_rate_limit.o

# 逐级添加下层依赖（l_2 仅依赖 l_1；高级在前、低级在后）
LIBS    += -lqcrypto -lqrand -lqmem -lqutil -lqlog

include ../../../include/Makefile.inc

install_hdr:
	cp -f q_sec.h $(INCLUDE)/q_sec.h

all: lib_static lib_so install_lib install_hdr

ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_sec_ut.c -o ut/bin/q_sec_ut -L$(LIB) -lqsec -lqcrypto -lqrand -lqmem -lqutil -lqlog -lpthread -Wl,-rpath,$(LIB)
	./ut/bin/q_sec_ut

.PHONY: install_hdr ut
