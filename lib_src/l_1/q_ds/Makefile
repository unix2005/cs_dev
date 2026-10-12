# ==============================================
#  lib_src/l_1/q_ds  ——  l_1 基础层 数据结构库（libqds）
#  首行必须引入公共配置
#  目录结构（见 AI_SKILL_RULE §3.1）：
#    headers.h      聚合公共基础头（include_*.h），仅作 api/ 源文件聚合入口，不安装
#    q_ds.h         对外头文件（自包含，外部可直接 #include <q_ds.h>）
#    Makefile
#    api/           C 源代码（q_ds_*.c，每函数一文件）
#    ut/            单元测试 / 小程序（q_ds_ut.c）
#  源文件引用铁律：api/ 下每个 .c 仅 #include "headers.h" 与 "q_ds.h"
#
#  说明：TARGET / TARGET_SO / OBJS 在 include 之前定义，使 Makefile.inc 的
#        lib_static / lib_so 能正确捕获先决条件。
#        静态库 / 动态库 / 安装均由 Makefile.inc 通用目标完成，本模块仅做：
#          - 对外头归集 install_hdr
#          - 单元测试 ut
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqds.a
TARGET_SO = libqds.so
OBJS   = api/q_ds_list_create.o api/q_ds_list_destroy.o \
         api/q_ds_list_push_back.o api/q_ds_list_push_front.o \
         api/q_ds_list_pop_front.o api/q_ds_list_find.o \
         api/q_ds_list_remove.o api/q_ds_list_size.o api/q_ds_list_clear.o \
         api/q_ds_map_create.o api/q_ds_map_destroy.o \
         api/q_ds_map_set.o api/q_ds_map_get.o api/q_ds_map_del.o \
         api/q_ds_map_has.o api/q_ds_map_size.o api/q_ds_map_clear.o \
         api/q_ds_vec_create.o api/q_ds_vec_destroy.o \
         api/q_ds_vec_push.o api/q_ds_vec_pop.o api/q_ds_vec_get.o \
         api/q_ds_vec_set.o api/q_ds_vec_size.o api/q_ds_vec_capacity.o api/q_ds_vec_clear.o

include ../../../include/Makefile.inc

# 分层依赖：l_1 基础层，零项目内依赖（不追加 -lq*），仅依赖 libc

# 对外头文件归集到 include/
install_hdr:
	cp -f q_ds.h $(INCLUDE)/q_ds.h

# 扩展默认 all：仅追加先决条件（无配方体，不触发"覆盖配方"告警）
all: lib_static lib_so install_lib install_hdr

# 单元测试：构建并运行 ut/ 小程序（链接 libqds.so，rpath 指向 lib/）
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_ds_ut.c -o ut/bin/q_ds_ut -L$(LIB) -lqds -lpthread -Wl,-rpath,$(LIB)
	./ut/bin/q_ds_ut

.PHONY: install_hdr ut
