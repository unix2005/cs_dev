# ==============================================
#  lib_src/l_2/q_ipc  ——  l_2 能力层 UDS 封装库（libqipc）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：Unix Domain Socket 封装（netd 与 bizd 进程间通信）
#  依赖：l_1（q_util / q_log）
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqipc.a
TARGET_SO = libqipc.so
OBJS   = api/q_ipc_connect.o \
         api/q_ipc_listen.o \
         api/q_ipc_accept.o \
         api/q_ipc_send.o \
         api/q_ipc_recv.o \
         api/q_ipc_close.o

# 逐级添加下层依赖（l_2 仅依赖 l_1）
LIBS    += -lqutil -lqlog

include ../../../include/Makefile.inc


all: lib_static lib_so install_lib install_hdr

ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_ipc_ut.c -o ut/bin/q_ipc_ut -L$(LIB) -lqipc -lqutil -lqlog -lpthread -Wl,-rpath,$(LIB)
	./ut/bin/q_ipc_ut

.PHONY: install_hdr ut
