# ==============================================
#  soft_src/knockd  ——  SPA 单包授权进程（UDP，仅 CAP_NET_ADMIN）后台服务（BUILD=bin）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：接收运维终端 UDP 单包授权（SM4-GCM + SM2 签名，由 q_spa 校验）；
#        校验通过将源 IP 加入本机 nftables allow set（TTL 300s）。
#  依赖闭包（高级在前、低级在后、传递闭包全列）：
#    q_common(信号/降权) q_spa q_crypto q_rand q_util q_mem q_log
#  注：knockd 为单线程 recvfrom 循环，暂不需要 pthread；
#      nftables/netlink 库待接入后在此追加；seccomp 待实现后追加 $(LIB_SECCOMP)
# ==============================================

BUILD   = bin
TARGET  = q_service_knockd
OBJS    = main.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so；
#    外部库(pthread / Tongsuo)仅在最终可执行程序链接期引入 ——
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqspa.a $(LIB)/libqcrypto.a \
         $(LIB)/libqrand.a $(LIB)/libqutil.a $(LIB)/libqmem.a $(LIB)/libqlog.a)
LIBS   = $(QLIBS) $(LIB_PTHREAD) -L$(SOFT_HOME)/tongsuo/lib64 $(LIB_CRYPTO) -Wl,-rpath,$(SOFT_HOME)/tongsuo/lib64

include ../../include/Makefile.inc
