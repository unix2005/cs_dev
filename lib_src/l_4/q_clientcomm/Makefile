# ==============================================
#  lib_src/l_4/q_clientcomm  ——  l_4 客户端通信内核（libqclientcomm）
#  跨三端共享的纯 C 通信内核；对外仅暴露 qc_client.h 的 C ABI 门面。
#  目录结构见 AI_SKILL_RULE §3.1；产出自包含，单模块 `make` 即可产出 include/ + lib/。
#  依赖闭包（高级在前、低级在后、传递闭包全列；仅供 BUILD=bin / ut 链接使用）：
#    q_chan(l_3) -> q_sec/q_codec/q_net(l_2) -> q_crypto/q_rand/q_reactor/q_util/q_mem/q_log(l_1)
#  注：本模块仅编译 .a/.so，lib_static/lib_so 不消费 LIBS；LIBS 仅供 ut / 最终可执行程序使用。
# ==============================================

TARGET    = libqclientcomm.a
TARGET_SO = libqclientcomm.so
HEADER    = qc_client.h
OBJS      = qc_client.o

# 逐级添加下层依赖（l_4 依赖 l_3 -> l_2 -> l_1）；仅供最终链接/ut 使用
LIBS     += -lqchan -lqsec -lqcodec -lqnet -lqcrypto -lqrand -lqreactor -lqutil -lqmem -lqlog

include ../../../include/Makefile.inc

# —— 单元测试（链接 libqclientcomm.so + 传递闭包；验证编译/链接/TLV 回环/空路径）——
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/qc_ut.c -o ut/bin/qc_ut \
	  -L$(LIB) -lqclientcomm -lqchan -lqsec -lqcodec -lqnet -lqcrypto \
	  -lqrand -lqreactor -lqutil -lqmem -lqlog \
	  $(LIB_PTHREAD) -L$(SOFT_HOME)/tongsuo/lib64 $(LIB_CRYPTO) \
	  -Wl,-rpath,$(LIB) -Wl,-rpath,$(SOFT_HOME)/tongsuo/lib64
	./ut/bin/qc_ut

.PHONY: install_hdr ut
