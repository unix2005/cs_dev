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
OBJS      = qc_client.o \
            qc_keeper_main.o qc_keeper_start.o qc_keeper_stop.o qc_keeper_state.o \
            qc_keeper_lock.o qc_keeper_unlock.o qc_keeper_note_result.o qc_keeper_wake.o \
            qc_keeper_set_fault.o qc_keeper_set_state.o qc_keeper_backoff_ms.o \
            qc_keeper_reauth.o qc_keeper_rebuild.o qc_set_reconnect_info.o

# 注：keeper 用平台原生线程（Windows=Win32 CRITICAL_SECTION/CreateThread，
#     其它=pthread），不再额外依赖 libq*；仍需 $(LIB_PTHREAD) 供 POSIX 侧链接。

# 逐级添加下层依赖（l_4 依赖 l_3 -> l_2 -> l_1）；仅供最终链接/ut 使用
# 注：qc_knock 用到 l_3/q_spa，故闭包内必须含 -lqspa（缺则 Windows ld 报未决符号）
LIBS     += -lqchan -lqspa -lqsec -lqcodec -lqnet -lqcrypto -lqrand -lqreactor -lqutil -lqmem -lqlog

# ut / ut_login 共用的链接闭包（静态库全在 $(LIB)，Tongsuo 走 DEP_HOME 依赖根）
UT_LIBS   = -L$(LIB) -lqclientcomm -lqchan -lqspa -lqsec -lqcodec -lqnet -lqcrypto \
            -lqrand -lqreactor -lqutil -lqmem -lqlog \
            $(LIB_PTHREAD) -L$(DEP_HOME)/tongsuo/lib64 $(LIB_CRYPTO) \
            -Wl,-rpath,$(LIB) -Wl,-rpath,$(DEP_HOME)/tongsuo/lib64

include ../../../include/Makefile.inc

# —— 单元测试（链接 libqclientcomm.so + 传递闭包；验证编译/链接/TLV 回环/空路径）——
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/qc_ut.c -o ut/bin/qc_ut $(UT_LIBS)
	./ut/bin/qc_ut

# —— 登录闭环端到端联调（需真实运行 netd + bizd + login；不自动执行）——
# 用法见 ut/qc_login_ut.c 头部注释；例：
#   ./ut/bin/qc_login_ut --host 127.0.0.1 --port 18099 --user qiaoshui \
#        --pass 123456 --pub /path/netd_sm2.pub
ut_login: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/qc_login_ut.c -o ut/bin/qc_login_ut $(UT_LIBS)
	@echo "已生成 ut/bin/qc_login_ut（需 netd/bizd/login 已启动，见 ut/qc_login_ut.c 头部用法）"

.PHONY: install_hdr ut ut_login
