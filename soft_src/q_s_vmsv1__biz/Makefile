# ==============================================
#  soft_src/q_s_vmsv1__biz  ——  业务微服务（bizd 下游 ep_biz=9003，TCP + JSON）后台服务（BUILD=bin）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：接收 bizd 转发的 JSON 信封，按 params.biz_type 返回业务数据。
#        M2 先打通 BIZ_QUERY(0x0020) + stats；companies/vehicles 留 M3。
#  依赖闭包（高级在前、低级在后、传递闭包全列；pthread 按需）：
#    q_common(信号/降权) q_reactor q_net(TCP 服务器/阻塞收发) q_log q_mem q_rand q_util + pthread
#  注：cJSON 走 include_xml.h；-lcjson 由 $(DEP_HOME)/cjson/lib 解析（不可依赖系统路径，
#      交叉编译/其它平台未必有系统 libcjson）。
# ==============================================

BUILD   = bin
TARGET  = q_service_biz
OBJS    = main.o biz_util.o biz_handler.o biz_srv.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so ——
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqreactor.a \
         $(LIB)/libqnet.a $(LIB)/libqlog.a $(LIB)/libqmem.a \
         $(LIB)/libqrand.a $(LIB)/libqutil.a)
LIBS   = $(QLIBS) $(LIB_PTHREAD) \
         -L$(DEP_HOME)/cjson/lib -lcjson -Wl,-rpath,$(DEP_HOME)/cjson/lib

# cJSON 头文件（include_xml.h -> cJSON.h）：必须走 SOFT_INCL，Makefile.inc 会展开成 -I
SOFT_INCL = . $(DEP_HOME)/cjson/include

include ../../include/Makefile.inc
