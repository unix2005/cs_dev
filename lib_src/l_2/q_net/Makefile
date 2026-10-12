# ==============================================
#  lib_src/l_2/q_net  ——  l_2 能力层 网络库（libqnet）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：socket 封装、UDP、客户端阻塞收发、Reactor+Worker 服务器；
#        通用事件循环与有界线程池复用 l_1/q_reactor（libqreactor），本层仅负责 socket 语义。
#  依赖：l_1（q_util / q_log / q_mem / q_reactor）
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqnet.a
TARGET_SO = libqnet.so
OBJS   = api/q_net_socket_tcp.o \
         api/q_net_set_nonblock.o \
         api/q_net_close.o \
         api/q_net_bind_listen.o \
         api/q_net_connect.o \
         api/q_net_socket_udp.o \
         api/q_net_socket_tcp6.o \
         api/q_net_socket_udp6.o \
         api/q_net_bind_listen6.o \
         api/q_net_connect6.o \
         api/q_net_bind.o \
         api/q_net_bind6.o \
         api/q_net_accept.o \
         api/q_net_udp_send.o \
         api/q_net_udp_recv.o \
         api/q_net_addr_resolve.o \
         api/q_net_addr_any.o \
         api/q_net_send_blocking.o \
         api/q_net_recv_blocking.o \
         api/q_net_srv_create.o \
         api/q_net_srv_destroy.o \
         api/q_net_srv_set_handler.o \
         api/q_net_srv_listen.o \
         api/q_net_srv_listen6.o \
         api/q_net_srv_run.o \
         api/q_net_srv_accept_handler.o \
         api/q_net_srv_conn_handler.o \
         api/q_net_srv_worker.o

# 逐级添加下层依赖（l_2 仅依赖 l_1；线程池经 q_reactor 引入，仍需 -lpthread）
LIBS    += -lqutil -lqlog -lqmem -lqreactor $(LIB_PTHREAD)

include ../../../include/Makefile.inc

install_hdr:
	cp -f q_net.h $(INCLUDE)/q_net.h

all: lib_static lib_so install_lib install_hdr

ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_net_ut.c -o ut/bin/q_net_ut -L$(LIB) -lqnet -lqutil -lqlog -lqmem -lqreactor $(LIB_PTHREAD) -Wl,-rpath,$(LIB)
	./ut/bin/q_net_ut

.PHONY: install_hdr ut
