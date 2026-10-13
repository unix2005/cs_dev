# ==============================================
#  soft_src/netd  ——  协议进程（公网 TCP）后台服务（BUILD=bin）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：SM2 密钥协商 / SM4-GCM 解包 / 字段校验 / 限流；经 q_ipc 转发 bizd。
#        安全基线：非 root + chroot + seccomp（详见部署方案文档）。
#  依赖闭包（高级在前、低级在后、传递闭包全列；pthread 按需）：
#    q_common(信号/降权) q_reactor q_net q_chan q_sec q_codec q_ipc
#    q_crypto q_rand q_util q_mem q_log + pthread
# ==============================================

BUILD   = bin
TARGET  = q_service_netd
OBJS    = main.o netd_config.o netd_util.o netd_limit.o \
         netd_ipc.o netd_handler.o netd_srv.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so；
#    外部库(pthread / Tongsuo)仅在最终可执行程序链接期引入 ——
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqreactor.a $(LIB)/libqxcfg.a \
         $(LIB)/libqnet.a $(LIB)/libqchan.a $(LIB)/libqsec.a \
         $(LIB)/libqcodec.a $(LIB)/libqipc.a $(LIB)/libqcrypto.a \
         $(LIB)/libqrand.a $(LIB)/libqutil.a $(LIB)/libqmem.a $(LIB)/libqlog.a)
# 注：q_xcfg 依赖系统 libxml2（与 knockd 一致）；pthread 供 reactor 线程池
LIBS   = $(QLIBS) $(LIB_PTHREAD) -lxml2 -L$(SOFT_HOME)/tongsuo/lib64 $(LIB_CRYPTO) -Wl,-rpath,$(SOFT_HOME)/tongsuo/lib64
# 注：seccomp 待 q_common 真正实现后，本处追加 $(LIB_SECCOMP)

include ../../include/Makefile.inc
