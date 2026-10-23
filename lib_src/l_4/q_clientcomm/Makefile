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
HEADERS   = qc_client.h qc_push.h
OBJS      = qc_client.o \
            qc_keeper_main.o qc_keeper_start.o qc_keeper_stop.o qc_keeper_state.o \
            qc_keeper_lock.o qc_keeper_unlock.o qc_keeper_note_result.o qc_keeper_wake.o \
            qc_keeper_set_fault.o qc_keeper_set_state.o qc_keeper_backoff_ms.o \
            qc_keeper_reauth.o qc_keeper_rebuild.o qc_set_reconnect_info.o \
            qc_conf_default.o qc_conf_load.o qc_conf_validate.o qc_conf_free.o \
            qc_conf_dump.o qc_conf_default_path.o \
            qc_push_create.o qc_push_destroy.o qc_push_set_callbacks.o \
            qc_push_start.o qc_push_stop.o qc_push_sub_delta.o \
            qc_push_connect.o qc_push_thread.o qc_push_wire.o

# 注：keeper 用平台原生线程（Windows=Win32 CRITICAL_SECTION/CreateThread，
#     其它=pthread），不再额外依赖 libq*；仍需 $(LIB_PTHREAD) 供 POSIX 侧链接。

# 逐级添加下层依赖（l_4 依赖 l_3 -> l_2 -> l_1）；仅供最终链接/ut 使用
# 注：qc_knock 用到 l_3/q_spa，故闭包内必须含 -lqspa（缺则 Windows ld 报未决符号）
# 注：qc_conf_* 用 l_1/q_xcfg（XML 配置），闭包内必须含 -lqxcfg；
#     libxml2 是系统/开源库，按惯例留到最终可执行链接期引入（.a 不链）。
LIBS     += -lqchan -lqspa -lqsec -lqcodec -lqnet -lqcrypto -lqrand -lqreactor -lqutil \
            -lqmem -lqlog -lqxcfg

# ut / ut_login 共用的链接闭包（静态库全在 $(LIB)，Tongsuo 走 DEP_HOME 依赖根）
UT_LIBS   = -L$(LIB) -lqclientcomm -lqchan -lqspa -lqsec -lqcodec -lqnet -lqcrypto \
            -lqrand -lqreactor -lqutil -lqmem -lqlog -lqxcfg -lxml2 \
            $(LIB_PTHREAD) -L$(DEP_HOME)/tongsuo/lib64 $(LIB_CRYPTO) \
            -Wl,-rpath,$(LIB) -Wl,-rpath,$(DEP_HOME)/tongsuo/lib64

include ../../../include/Makefile.inc

# M5：推送门面 qc_push.h 也是对外公共头（native 壳 vms_core 要消费），
# 覆盖 Makefile.inc 的单头安装规则，两个头一起归集到 include/。
install_hdr:
	@for h in $(HEADERS); do \
		if [ -f "$$h" ]; then cp -f "$$h" "$(INCLUDE)/" && echo "    $$h -> $(INCLUDE)/"; fi; \
	done

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

# —— 客户端配置层单元测试（qc_conf_*：XML 加载 / 默认值 / 校验 / 脱敏）——
# 不需要任何后台服务，单模块即可跑。
ut_conf: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/qc_conf_ut.c -o ut/bin/qc_conf_ut $(UT_LIBS)
	./ut/bin/qc_conf_ut

# —— M5 推送层单元测试（qc_push_wire 字节序/TLV 回环 + 订阅集合增删去重 + 分片边界）——
# 纯本地，不起线程不连网；真实长连接见 e2e_m5.sh。
ut_push: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/qc_push_ut.c -o ut/bin/qc_push_ut $(UT_LIBS)
	./ut/bin/qc_push_ut

.PHONY: install_hdr ut ut_login ut_conf ut_push
