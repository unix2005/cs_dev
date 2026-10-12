# ==============================================
#  lib_src/l_1/q_mem  —— 基础层内存库（libqmem）
#  首行必须引入公共配置
#  目录结构（见 AI_SKILL_RULE §3.1）：
#    headers.h      聚合公共基础头（include_*.h），仅作 api/ 源文件聚合入口，不安装
#    q_mem.h        对外头文件（自包含，外部可直接 #include <q_mem.h>）
#    Makefile
#    api/           C 源代码（q_mem_*.c / q_*_*.c，每函数一文件）
#    ut/            单元测试 / 小程序（q_mem_ut.c）
#  源文件引用铁律：api/ 下每个 .c 仅 #include "headers.h" 与 "q_mem.h"
#
#  说明：TARGET / TARGET_SO / OBJS 在 include 之前定义，使 Makefile.inc 的
#        lib_static / lib_so 能正确捕获先决条件（CFLAGS 已含 -Iinclude，故 q_mem.h
#        在 include/ 下可直接被外部以 <q_mem.h> 引用）。
#        静态库 / 动态库 / 安装均由 Makefile.inc 通用目标完成，本模块仅做：
#          - 对外头归集 install_hdr（inc 的 install_lib 不负责头文件）
#          - 单元测试 ut
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqmem.a
TARGET_SO = libqmem.so
OBJS   = api/q_mem_zero.o api/q_mem_secure_free.o \
         api/q_mempool_create.o api/q_mempool_alloc.o api/q_mempool_free.o \
         api/q_mempool_clear.o api/q_mempool_destroy.o api/q_mempool_avail.o \
         api/q_buf_create.o api/q_buf_append.o api/q_buf_len.o \
         api/q_buf_data.o api/q_buf_clear.o api/q_buf_destroy.o

include ../../../include/Makefile.inc

# 分层依赖：l_1 基础层，零项目内依赖，仅依赖 libc / OS syscall
#           不依赖任何其它 q_* 库，故 LIBS 保持默认空（不追加 -lq*）

# 对外头文件归集到 include/（include 目录由项目初始化时创建，此处不再建子目录）

# 扩展默认 all：仅追加先决条件（无配方体，不触发“覆盖配方”告警）
all: lib_static lib_so install_lib install_hdr

# 单元测试：构建并运行 ut/ 小程序（链接 libqmem.so，rpath 指向 lib/）
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_mem_ut.c -o ut/bin/q_mem_ut -L$(LIB) -lqmem -lpthread -Wl,-rpath,$(LIB)
	./ut/bin/q_mem_ut

.PHONY: install_hdr ut
