# ==============================================
#  soft_src/knockd  ——  SPA 单包授权进程（UDP，仅 CAP_NET_ADMIN）后台服务（BUILD=bin）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：接收运维终端 UDP 单包授权（SM4-GCM + SM2 签名，由 q_spa 校验）；
#        校验通过将源 IP 加入本机 nftables allow set（TTL 300s）。
#  依赖闭包（高级在前、低级在后、传递闭包全列）：
#    q_common(信号/降权) q_spa q_crypto q_rand q_util q_mem q_log
#  注：knockd 为单线程 recvfrom 循环，暂不需要 pthread；
#      nftables 直连用系统 libnftables.so.1（netlink，无 nft 命令行依赖）；
#      seccomp 沙箱已移入共享库 libqcommon（daemon_seccomp_apply，profile=3=knockd），
#      链接 -lseccomp 由该库符号拉取（libseccomp 高层 API，收敛 syscall）
# ==============================================

BUILD   = bin
TARGET  = q_service_knockd
OBJS    = main.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so；
#    外部库(pthread / Tongsuo)仅在最终可执行程序链接期引入 ——
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqreactor.a $(LIB)/libqxcfg.a $(LIB)/libqspa.a $(LIB)/libqcrypto.a \
         $(LIB)/libqrand.a $(LIB)/libqutil.a $(LIB)/libqmem.a $(LIB)/libqlog.a \
         $(LIB)/libqnet.a)
# 系统 libnftables.so.1（netlink 直连内核，无需 nft 命令）；用 -l: 形式链接具体 SONAME
# （目标机未装 libnftables-dev，无 libq... 链接名）。运行期由 ldconfig 解析。
# seccomp 沙箱已移入 libqcommon（daemon_seccomp_apply）；-lseccomp 由该库符号拉取，
# 此处保留以确保链接期解析（libqcommon.a 引用 libseccomp）。
LIBS   = $(QLIBS) $(LIB_PTHREAD) -lxml2 -L$(SOFT_HOME)/tongsuo/lib64 $(LIB_CRYPTO) -Wl,-rpath,$(SOFT_HOME)/tongsuo/lib64 -l:libnftables.so.1 -lseccomp

include ../../include/Makefile.inc
