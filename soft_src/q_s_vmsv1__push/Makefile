# ==============================================
#  soft_src/q_s_vmsv1__push  ——  实时推送微服务（pushd，netd_B 下游 ep_push，UDS）后台服务（BUILD=bin）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：M5 推送栈的下游微服务，与 q_s_vmsv1__biz 平级。每 UDS 连接 = 一条推送客户端
#        长连接会话：BEGIN 登记（token 校验 + 用户名→组织）/ SUB_DELTA 增量订阅集合 /
#        模拟线程按订阅下发 TRAJ、按组织子树过滤下发 ALARM。桩数据（6 车 + 3 报警）不连库。
#  wire 契约：netd_B ↔ pushd 为 [4B BE len][TLV...]，TLV tag 与客户端密文体内层共用
#        （include_ipc_proto.h，三端唯一事实来源，勿在本服务重复定义）。
#  并发模型：每连接一个 worker 独占读；模拟线程统一写；全部 socket 写在 g_lock 内串行化
#        （测试桩会话数个位数，刻意不预支每会话写锁复杂度）。
#  依赖闭包（高级在前、低级在后、传递闭包全列；pthread/m 按需）：
#    q_common(信号/降权) q_reactor(循环+线程池) q_net(socket) q_ipc(UDS) q_codec(TLV)
#    q_log q_mem q_util + pthread + m(桩数据 cos/sin)
#  注：不连 cJSON/libxml2 —— 推送域 TLV 直接由 q_codec 编解码，无 JSON 信封。
# ==============================================

BUILD   = bin
TARGET  = q_service_push
OBJS    = main.o push_util.o push_handler.o push_stub.o push_sim.o push_srv.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so ——
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqreactor.a \
         $(LIB)/libqnet.a $(LIB)/libqipc.a $(LIB)/libqcodec.a \
         $(LIB)/libqlog.a $(LIB)/libqmem.a $(LIB)/libqutil.a)
LIBS   = $(QLIBS) $(LIB_PTHREAD) $(LIB_M)

SOFT_INCL = .

include ../../include/Makefile.inc
