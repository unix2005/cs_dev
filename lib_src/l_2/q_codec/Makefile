# ==============================================
#  lib_src/l_2/q_codec  ——  l_2 能力层 帧编解码库（libqcodec）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：pkt_hdr 解析/序列化、TLV 编解码、字段校验（仅结构编解码，不做加解密）
#  依赖：l_1（本模块为纯结构编解码，不链接任何 l_1 库）
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqcodec.a
TARGET_SO = libqcodec.so
OBJS   = api/q_codec_pkt_hdr_pack.o \
         api/q_codec_pkt_hdr_unpack.o \
         api/q_codec_tlv_encode.o \
         api/q_codec_tlv_decode.o \
         api/q_codec_field_validate.o

include ../../../include/Makefile.inc

# 对外头文件归集到 include/

# 扩展默认 all：仅追加先决条件（无配方体）
all: lib_static lib_so install_lib install_hdr

# 单元测试
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_codec_ut.c -o ut/bin/q_codec_ut -L$(LIB) -lqcodec -lpthread -Wl,-rpath,$(LIB)
	./ut/bin/q_codec_ut

.PHONY: install_hdr ut
