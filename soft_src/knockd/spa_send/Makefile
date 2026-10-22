# spa_send —— knockd 测试发送端（仅联调 / 验证开门闭环用）
# 产物：可执行程序 spa_send -> $(BIN)/
# 用法：spa_send [target_host] [target_port] [req_port] [req_ttl] [user_id]
# 依赖：q_spa（SPA 包构造）+ q_crypto（SM4-GCM，基于 Tongsuo）+ q_rand / q_mem / q_log
# 构建：与 knockd 同处 Linux 构建环境执行 `make`（会落到 bin/）
TARGET = spa_send
OBJS   = main.o
BUILD  = bin
SOFT_INCL = .

# q_spa 传递依赖：q_crypto 国密基于 Tongsuo；
# 本项目 q_* 库一律静态链入（避免依赖动态 libqcrypto.so 缺失 Tongsuo 符号），
# Tongsuo 仅在最终 exe 链接期引入（与 knockd 同约定）。
# 依赖根走 DEP_HOME（与 knockd/Makefile 一致）：交叉编译时用
# `make LIB=<dir> DEP_HOME=<dir>` 覆盖，否则三端产物会互相覆盖。
QLIBS = $(call ld_group, \
        $(LIB)/libqspa.a $(LIB)/libqcrypto.a $(LIB)/libqrand.a $(LIB)/libqmem.a $(LIB)/libqlog.a)
LIBS  = $(QLIBS) -L$(DEP_HOME)/tongsuo/lib64 $(LIB_CRYPTO) -Wl,-rpath,$(DEP_HOME)/tongsuo/lib64

include ../../../include/Makefile.inc
