# q_reactor 模块 Makefile（libqreactor）
# 约定见 include/Makefile.inc：TARGET 由 q_ 模块名去下划线推导为 libqreactor(.a/.so)
TARGET    = libqreactor.a
TARGET_SO = libqreactor.so

# 通用对象（所有平台都编译；事件源/定时器内部已按平台宏自行裁剪）
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
    api/q_reactor_timer_arm.o \
    api/q_reactor_timer_destroy.o \
    api/q_reactor_timer_common.o

# 事件源插件：按平台选择其一（epoll / kqueue / WSAPoll 各自依赖不同系统头，不能跨平台混编）
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
OBJS += \
    api/q_evsrc_epoll_create.o \
    api/q_evsrc_epoll_add.o \
    api/q_evsrc_epoll_del.o \
    api/q_evsrc_epoll_destroy.o \
    api/q_evsrc_epoll_src.o
else ifneq (,$(filter Darwin FreeBSD,$(UNAME_S)))
OBJS += \
    api/q_evsrc_kqueue_create.o \
    api/q_evsrc_kqueue_add.o \
    api/q_evsrc_kqueue_del.o \
    api/q_evsrc_kqueue_destroy.o \
    api/q_evsrc_kqueue_src.o
else
# Windows（MinGW/MSYS：uname 通常为 MINGW*_NT-* / MSYS_NT-*）或其它：WSAPoll 事件源
OBJS += \
    api/q_evsrc_poll_create.o \
    api/q_evsrc_poll_add.o \
    api/q_evsrc_poll_del.o \
    api/q_evsrc_poll_destroy.o \
    api/q_evsrc_poll_src.o
endif

# q_reactor 不依赖其它 q_* 模块（事件源/定时器为平台系统调用，worker 池/分发核心均在本模块内）
# pthread 为 worker 池/事件源所需，属系统库，按需加入（见 include/Makefile.inc 的 LIB_PTHREAD）

include ../../../include/Makefile.inc

LIBS += $(LIB_PTHREAD)

# 单元测试（Linux 下：make ut  ->  ut/bin/q_reactor_ut）
.PHONY: ut
ut: $(OBJS) ut/q_reactor_ut.c
	$(CC) $(CFLAGS) $(OBJ_CFLAGS) -o ut/bin/q_reactor_ut ut/q_reactor_ut.c $(OBJS) $(LDFLAGS) $(LIBS)
