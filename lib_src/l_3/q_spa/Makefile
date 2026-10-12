# ==============================================
#  lib_src/l_3/q_spa  ——  l_3 协议层 SPA 单包授权库（libqspa）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：单包授权报文构造 / 校验（SM4-GCM，定长无响应，抗 UDP 反射放大）。
#  依赖：l_1（q_crypto 国密、q_rand 随机）；不依赖 l_2 / l_3（避免同层耦合）。
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqspa.a
TARGET_SO = libqspa.so
OBJS   = api/q_spa_build.o \
         api/q_spa_verify.o

# 唯一允许直接链接 Tongsuo：补充库路径 + 运行时 rpath（注意 LIB_CRYPTO 在 include 之后定义）
LIBS += $(SOFT_HOME)/tongsuo/lib64 

include ../../../include/Makefile.inc


all: lib_static lib_so install_lib install_hdr

# 单元测试：构建并运行 ut/ 小程序（链接 libqspa.so，rpath 指向 lib/）
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_spa_ut.c -o ut/bin/q_spa_ut -L$(LIB) -L$(LIBS) -lqspa -lqcrypto -lqrand -lqutil -lqmem -lqlog $(LIB_CRYPTO) -Wl,-rpath,$(LIB) -Wl,-rpath,$(LIBS)
	./ut/bin/q_spa_ut

.PHONY: install_hdr ut
