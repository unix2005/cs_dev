# ==============================================
#  lib_src/l_3/q_chan  ——  l_3 协议层 安全通道 + 协议语义库（libqchan）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：安全通道（SM2 ECDH / SM4-GCM / 通道绑定）、协议状态机、指令分发、会话 Token。
#  依赖：l_1（q_crypto 国密原语、q_rand 随机数、q_mem、q_log、q_util）
#        l_2（q_sec 密钥派生/seq 窗口/PoW、q_codec 帧编解码、q_net 阻塞收发）
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqchan.a
TARGET_SO = libqchan.so
OBJS   = api/q_chan_binding.o \
         api/q_chan_create.o \
         api/q_chan_destroy.o \
         api/q_chan_disp_create.o \
         api/q_chan_disp_destroy.o \
         api/q_chan_disp_invoke.o \
         api/q_chan_disp_register.o \
         api/q_chan_handshake.o \
         api/q_chan_iv_derive.o \
         api/q_chan_receive.o \
         api/q_chan_recv.o \
         api/q_chan_send.o \
         api/q_chan_sm_cmd_allowed.o \
         api/q_chan_sm_on_cmd.o \
         api/q_chan_sm_transition.o \
         api/q_chan_token_issue.o \
         api/q_chan_token_validate.o \
         api/q_chan_transmit.o

# 逐级添加下层依赖（l_3 依赖 l_1 + l_2；高级在前、低级在后）
# 注：LIBS 仅用于 BUILD=bin 与 ut 链接；动态库编译不消费 LIBS（依赖以未决符号保留，
#     运行期经 lib/ 的 RUNPATH 统一解析）。下列按传递闭包显式列出，因 .so 不记录 NEEDED。
LIBS    += -lqsec -lqcodec -lqnet -lqcrypto -lqrand -lqreactor -lqutil -lqmem -lqlog

include ../../../include/Makefile.inc

# q_net -> q_reactor 使用 pthread（libqreactor.so 未记录 NEEDED libpthread），按需加入
LIBS    += $(LIB_PTHREAD)


all: lib_static lib_so install_lib install_hdr

# 单元测试：构建并运行 ut/ 两个小程序（链接 libqchan.so，rpath 指向 lib/）
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_chan_ut.c -o ut/bin/q_chan_ut -L$(LIB) -lqchan $(LIBS) -Wl,-rpath,$(LIB)
	./ut/bin/q_chan_ut
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_chan_proto_ut.c -o ut/bin/q_chan_proto_ut -L$(LIB) -lqchan $(LIBS) -Wl,-rpath,$(LIB)
	./ut/bin/q_chan_proto_ut

.PHONY: install_hdr ut
