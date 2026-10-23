/**
 * @file include_ipc_proto.h
 * @brief netd <-> bizd 进程间通信 wire 契约（双方共用的唯一事实来源）
 * @platform Windows/Linux/macOS
 *
 * @note 本文件【只定义线路格式】相关的常量，不含业务语义、不含加解密。
 *       两侧公共头（srv_netd.h / srv_bizd.h）统一包含本文件，
 *       **严禁在 netd 与 bizd 各自重复定义同一 tag / 结果码**，
 *       否则一端改动另一端不知，会产生难以排查的协议漂移。
 *
 * 分帧（Unix Socket 侧与下游 TCP 侧一致）：
 *     [4 字节大端长度][负载]     长度字段不含前缀本身，单位为字节
 * 负载：TLV 序列，单条 TLV 布局 tag(2,网络序) + len(2,网络序) + val(len)
 *
 * 下游微服务（另一项目）采用 JSON 信封，字段见下方「JSON 包格式」注释。
 */
#ifndef INCLUDE_IPC_PROTO_H
#define INCLUDE_IPC_PROTO_H

#include <stdint.h>

/* ===================== 分帧 ===================== */
#define Q_IPC_LENPFX   4u                  /* 长度前缀字节数 */
#define Q_IPC_MAXMSG   (256u * 1024u)      /* 单条消息负载上限 */
#define Q_IPC_TLV_MAX  60000u              /* 单条 TLV val 上限（len 为 uint16） */

/* ===================== 外层 TLV tag（netd <-> bizd） ===================== */
#define Q_IPC_T_CMD     0x0001u  /* 业务指令字   val: uint16 网络序 */
#define Q_IPC_T_BODY    0x0002u  /* 业务体       val: 内层 TLV 序列 */
#define Q_IPC_T_SRCIP   0x0003u  /* 客户端源 IP  val: [1B 族:4=IPv4 / 6=IPv6][16B 地址]
                                      地址扩为 16 字节以支持 IPv6；IPv4 时地址占低 4 字节
                                      （高 12 字节为 0），旧版 netd 仅发 4 字节时按 IPv4 兼容解析 */
#define Q_IPC_T_CHANID  0x0004u  /* 通道绑定 ID  val: 32B */
#define Q_IPC_T_USERID  0x0005u  /* SM3(用户名)  val: 32B */
#define Q_IPC_T_TOKEN   0x0006u  /* 会话 Token   val: 32B 或 148B 规范串 */
#define Q_IPC_T_RESULT  0x0007u  /* 结果码       val: uint32 网络序 */
#define Q_IPC_T_REQID   0x0008u  /* 请求号       val: 字符串（netd 生成，透传 bizd / 微服务；
                                      与 bizd->微服务的 JSON "id" 一致，用于跨 netd/bizd/微服务
                                      日志对账，grep 同一 src_ip#id 即可关联整条链） */

/* ===================== 结果码 ===================== */
#define Q_IPC_R_OK            0u   /* 成功 */
#define Q_IPC_R_UNAVAILABLE   1u   /* 下游微服务不可达 / 转发失败 */
#define Q_IPC_R_BADREQ        2u   /* 请求参数 / TLV 结构非法 */
#define Q_IPC_R_UNAUTHORIZED  3u   /* Token 无效 / 过期 / 绑定不匹配 */
#define Q_IPC_R_FORBIDDEN    4u   /* 权限不足 */
#define Q_IPC_R_INTERNAL     5u   /* 内部错误 */
#define Q_IPC_R_UNSUPPORTED  6u   /* 指令/子类型未实现（下游微服务回；客户端应提示"功能未开放"） */

/* ===================== 内层 TLV tag（客户端 <-> netd 密文体 / netd <-> bizd body） ===================== */
#define Q_IPC_T_USERNAME  0x0101u  /* 用户名 */
#define Q_IPC_T_PWDHASH   0x0102u  /* 口令派生值 */
#define Q_IPC_T_OTP       0x0103u  /* 动态口令 */
#define Q_IPC_T_DEVID     0x0104u  /* 设备标识 */
#define Q_IPC_T_CVER      0x0105u  /* 客户端版本 */
#define Q_IPC_T_AUTHTOK   0x0106u  /* 会话 Token（业务请求随带回） */

/* ===================== 业务参数段（客户端 <-> netd 密文体，随业务请求携带） =====================
 * 背景：bizd 的 body_to_params() 对未知 tag 只能退化成 "tag_%04x" 的 hex 字段，
 *       下游能解但可读性差且易漂移，故在此统一定义（三端客户端 / bizd / 微服务共用）。
 *
 * 承载类型（**必须**严格遵守，否则 bizd 解出来的值语义就错了）：
 *   Q_IPC_T_BIZ_TYPE   : UTF-8 文本（如 "stats" / "companies" / "vehicles"），无内嵌 NUL
 *   Q_IPC_T_BIZ_PARAM  : UTF-8 文本（推荐 JSON 对象，如 {"company_id":123}），无内嵌 NUL
 *   Q_IPC_T_PAGE_OFFSET: uint32 大端
 *   Q_IPC_T_PAGE_LIMIT : uint32 大端
 *
 * 请求体固定形状（业务指令）：
 *   0x0106 AUTHTOK(32B) + 0x0201 BIZ_TYPE + [0x0202 BIZ_PARAM] + [0x0203 OFFSET] + [0x0204 LIMIT]
 *
 * 注：0x0101~0x0106 这些"凭据类"字段在 bizd 侧仍按 hex 字符串传给下游（登录服务已按 hex 解码），
 *     本段的新 tag 按上表语义传（文本原样 / 数值），**不要**改成 hex —— 一端改即协议漂移。 */
#define Q_IPC_T_BIZ_TYPE    0x0201u  /* 业务子类型（文本） */
#define Q_IPC_T_BIZ_PARAM   0x0202u  /* 业务参数（文本，推荐 JSON） */
#define Q_IPC_T_PAGE_OFFSET 0x0203u  /* 分页偏移（uint32 BE） */
#define Q_IPC_T_PAGE_LIMIT  0x0204u  /* 每页条数（uint32 BE） */

/* ===================== 推送域 TLV（M5：netd_B <-> q_s_vmsv1__push / 密文体内层） =====================
 * 三处共用同一组 tag，严禁各端重复定义：
 *   1) 客户端 <-> netd_B 的密文体内层（SUB_DELTA 请求体 / TRAJ / ALARM 负载）；
 *   2) netd_B <-> pushd 的 UDS 帧内层（Q_IPC_T_BODY 里就是这组 tag）；
 *   3) pushd 内部会话/订阅集合的解析。
 *
 * 承载类型（必须严格遵守）：
 *   Q_IPC_T_PUSH_OP      : 1B（1=ADD / 2=REMOVE）
 *   Q_IPC_T_PUSH_VID     : UTF-8 文本（车辆主键，无 NUL；SUB_DELTA 中可多次出现）
 *   Q_IPC_T_PUSH_LON/LAT : 8B double 的 IEEE754 位型大端（memcpy 到 uint64 后逐字节 BE）
 *   Q_IPC_T_PUSH_SPEED   : uint32 BE（km/h）
 *   Q_IPC_T_PUSH_TV      : UTF-8 文本（"YYYY-MM-DD HH:MM:SS"）
 *   Q_IPC_T_PUSH_ADDR    : UTF-8 文本（位置描述）
 *   Q_IPC_T_PUSH_ONLINE  : uint32 BE（0/1）
 *   Q_IPC_T_PUSH_AFLAGS  : uint32 BE（报警位掩码，VMS_ALARM_*）
 *   Q_IPC_T_PUSH_ATYPE   : uint32 BE（报警类型，VMS_ALARM_* 单值）
 *   Q_IPC_T_PUSH_ATEXT   : UTF-8 文本（报警描述）
 *   Q_IPC_T_PUSH_ORG     : UTF-8 文本（报警所属组织 org_id）
 *   Q_IPC_T_PUSH_PLATE   : UTF-8 文本（车牌）
 *
 * 推送【不分页】（2026-10-23 裁定）：轨迹/报警都是按车辆逐条事件驱动下发，单帧几十字节，
 * 不存在"一页 N 条"。唯一受 Q_IPC_TLV_MAX(60000) 约束的是 C→S 的大订阅集合：
 * 此时用【分片 delta】—— 一次大 ADD/REMOVE 拆成多帧 PUSH_SUB_DELTA（每帧 <= 500 个 ID），
 * 服务端逐帧合并；分片是同一语义的增量续传，没有 page 序号，与分页查询本质不同。 */
#define Q_IPC_T_PUSH_OP     0x0301u  /* 订阅操作：1B（1=ADD / 2=REMOVE） */
#define Q_IPC_T_PUSH_VID    0x0302u  /* 车辆 ID（UTF-8 文本；SUB_DELTA 可多次出现） */
#define Q_IPC_T_PUSH_LON    0x0303u  /* 经度：double 位型大端 8B */
#define Q_IPC_T_PUSH_LAT    0x0304u  /* 纬度：double 位型大端 8B */
#define Q_IPC_T_PUSH_SPEED  0x0305u  /* 速度：uint32 BE（km/h） */
#define Q_IPC_T_PUSH_TV     0x0306u  /* 轨迹时间：UTF-8 文本 */
#define Q_IPC_T_PUSH_ADDR   0x0307u  /* 位置描述：UTF-8 文本 */
#define Q_IPC_T_PUSH_ONLINE 0x0308u  /* 在线标志：uint32 BE */
#define Q_IPC_T_PUSH_AFLAGS 0x0309u  /* 报警位掩码：uint32 BE */
#define Q_IPC_T_PUSH_ATYPE  0x030Au  /* 报警类型：uint32 BE */
#define Q_IPC_T_PUSH_ATEXT  0x030Bu  /* 报警描述：UTF-8 文本 */
#define Q_IPC_T_PUSH_ORG    0x030Cu  /* 报警所属组织 org_id：UTF-8 文本 */
#define Q_IPC_T_PUSH_PLATE  0x030Du  /* 车牌：UTF-8 文本 */

/* 订阅操作取值（Q_IPC_T_PUSH_OP 的 val） */
#define Q_PUSH_OP_ADD    1u
#define Q_PUSH_OP_REMOVE 2u

/* 报警类型（Q_IPC_T_PUSH_ATYPE / AFLAGS 的取值）。
   与 native ABI 的 VMS_ALARM_* **必须同值** —— vms_core.c 内有静态断言把守，
   一端改另一端编译期即失败，不会静默错位。 */
#define Q_PUSH_ALARM_NONE      0x0000u
#define Q_PUSH_ALARM_OVERSPEED 0x0001u  /* 超速 */
#define Q_PUSH_ALARM_FATIGUE   0x0002u  /* 疲劳驾驶 */
#define Q_PUSH_ALARM_OFFLINE   0x0004u  /* 掉线 */
#define Q_PUSH_ALARM_GEOFENCE  0x0008u  /* 越界 */
#define Q_PUSH_ALARM_GNSS_LOST 0x0010u  /* GNSS 失锁 */

/* 单帧 PUSH_SUB_DELTA 最多携带的车辆 ID 数（分片阈值）。
   500 x (4+2+40) ≈ 23KB，留足 Q_IPC_TLV_MAX(60000) 余量（netd 还要包外层 TLV）。 */
#define Q_PUSH_SHARD_MAX 500u

/* ===================== 口令派生参数（方案A：服务端下发 salt，客户端本地派生） =====================
 * 客户端（Windows/Linux/macOS 三端）与登录服务【必须】使用同一组 (salt, iter)：
 * salt 由登录服务持有、经 SALT_QUERY(cmd=4) 下发；iter 作为协议常量定义在此，
 * 一端改动即协议漂移，故严禁各自硬编码。 */
#define Q_PBKDF2_ITER_DEFAULT  100000u  /* PBKDF2-HMAC-SM3 迭代次数（等保/护网：>=100000） */
#define Q_PBKDF2_SALT_LEN      16u      /* salt 字节数（16B -> 32 hex） */
#define Q_PBKDF2_OUT_LEN       32u      /* pwd_hash 字节数（SM3 输出长度，32B -> 64 hex） */

/* ===================== bizd -> 微服务 JSON 包格式 =====================
 *
 * 【请求】bizd -> 微服务
 * {
 *   "ver": 1,                     // 协议版本，固定 1
 *   "id":  "17",                  // 请求 id（十进制字符串），响应原样回带用于关联
 *   "cmd": 32,                       // 指令字（数值，与 §5.3 指令表一致）
 *   "cmd_name": "BIZ_QUERY",      // 指令名（可读，便于排障；微服务以 cmd 为准）
 *   "ctx": {                         // 安全上下文（由 netd 转发，微服务用于鉴权/审计）
 *     "src_ip":    "203.0.113.7", // 客户端真实源 IP（netd 侧已解 ProxyProtocol）
 *     "user_id":   "<64 hex>",    // SM3(用户名)
 *     "device_id": "<64 hex>",    // 设备指纹
 *     "channel_id":"<64 hex>"     // SM3(session_key)，通道绑定
 *   },
 *   "token": "<64 hex>",          // 会话 Token（源自密文体；LOGIN 时为空串）
 *   "params": { }                    // 业务参数（由业务体 TLV 解码而来，键值对）
 * }
 *
 * 【响应】微服务 -> bizd
 * {
 *   "ver": 1,
 *   "id":  "17",                     // 回带请求 id
 *   "result": 0,                     // 结果码，见上表
 *   "message": "ok",                 // 可读描述（失败时为错误原因，不入审计明文敏感信息）
 *   "data": { }                   // 业务数据；bizd 原样序列化后回传 netd
 * }
 *
 * 约定：
 *   - 所有整数字段在网络上传输时统一用【大端】；JSON 中数值字段保持十进制数字。
 *   - 二进制字段（token / user_id / device_id / channel_id）在 JSON 中用【小写 hex 字符串】表示。
 *   - 单包负载上限 256KB；超限直接断连。
 *   - 未知 cmd / 非法 TLV -> RESULT=BADREQ 并断连。
 */

#endif /* INCLUDE_IPC_PROTO_H */
