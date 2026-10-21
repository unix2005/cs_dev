# ==============================================
#  soft_src/q_s_vmsv1__login  ——  登录微服务（bizd 下游，TCP + JSON）后台服务（BUILD=bin）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：接收 bizd 转发的 JSON 信封，完成登录鉴权并签发 Token，回写 JSON 响应。
#        用于打通 客户端 -> netd -> bizd -> login 全链路测试。
#  依赖闭包（高级在前、低级在后、传递闭包全列；pthread 按需）：
#    q_common(信号/降权) q_reactor q_net(TCP 服务器/阻塞收发) q_log q_mem q_rand + pthread
#  注：cJSON 走 include_xml.h；-lcjson 由 -L$(LIB)（lib/）或系统路径解析。
# ==============================================

BUILD   = bin
TARGET  = q_service_login
OBJS    = main.o login_util.o login_handler.o login_srv.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so ——
# cJSON（vendored，走 DEP_HOME 依赖根）+ pthread；
# 注：不可只写 -lcjson 依赖系统路径 —— 交叉编译/其它平台未必有系统 libcjson。
#     本服务还需要 q_crypto 的 PBKDF2-SM3（口令派生），故闭包里含 libqcrypto。
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqreactor.a \
         $(LIB)/libqnet.a $(LIB)/libqlog.a $(LIB)/libqmem.a \
         $(LIB)/libqrand.a $(LIB)/libqcrypto.a $(LIB)/libqutil.a)
LIBS   = $(QLIBS) $(LIB_PTHREAD) \
         -L$(DEP_HOME)/cjson/lib -lcjson -Wl,-rpath,$(DEP_HOME)/cjson/lib \
         -L$(DEP_HOME)/tongsuo/lib64 $(LIB_CRYPTO) -Wl,-rpath,$(DEP_HOME)/tongsuo/lib64

# cJSON 头文件（include_xml.h -> cJSON.h）：必须走 SOFT_INCL，Makefile.inc 会展开成 -I
SOFT_INCL = . $(DEP_HOME)/cjson/include

include ../../include/Makefile.inc
