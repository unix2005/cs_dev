# q_reactor 模块 Makefile（libqreactor）
# 约定见 include/Makefile.inc：TARGET 由 q_ 模块名去下划线推导为 libqreactor(.a/.so)
TARGET    = libqreactor.a
TARGET_SO = libqreactor.so

OBJS = \
    api/q_disp_create.o \
    api/q_disp_destroy.o \
    api/q_disp_submit.o \
    api/q_disp_submit_local.o \
    api/q_disp_add_src.o \
    api/q_disp_run.o \
    api/q_disp_loop.o \
    api/q_disp_wake.o \
    api/q_disp_stop.o \
    api/q_evsrc_epoll_create.o \
    api/q_evsrc_epoll_add.o \
    api/q_evsrc_epoll_del.o \
    api/q_evsrc_epoll_destroy.o \
    api/q_evsrc_epoll_src.o \
    api/q_reactor_create.o \
    api/q_reactor_destroy.o \
    api/q_reactor_add.o \
    api/q_reactor_del.o \
    api/q_reactor_run.o \
    api/q_reactor_loop.o \
    api/q_tpool_create.o \
    api/q_tpool_destroy.o \
    api/q_tpool_dispatch.o \
    api/q_reactor_timer_create.o \
    api/q_reactor_timer_arm.o

# q_reactor 不依赖其它 q_* 模块（epoll/timer 为平台系统调用，worker 池/分发核心均在本模块内）
LIBS =

include ../../../include/Makefile.inc

# 单元测试（Linux 下：make ut  ->  ut/bin/q_reactor_ut）
.PHONY: ut
ut: $(OBJS) ut/q_reactor_ut.c
	$(CC) $(CFLAGS) $(OBJ_CFLAGS) -o ut/bin/q_reactor_ut ut/q_reactor_ut.c $(OBJS) $(LDFLAGS)
