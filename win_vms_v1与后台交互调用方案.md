# win_vms_v1 与后台交互 —— 调用方案（不含代码）

> 结论先行：**win_vms_v1 只应通过一层 C ABI 门面与后台交互**，即 `include/qc_client.h` 的 `qc_*` 函数集;
> C# 侧保持现有的 `vms_*` P/Invoke 形状不变，由 `vms_core.dll` 内部把 `vms_*` 翻译成 `qc_*`。
> 这样 UI 层零改动、Mock 降级路径保留，同时不把 `q_fd_t` / `q_chan_t` / TLV / 国密细节暴露给 C#。

---

## 1. 调用链与分层

```
WinUI3 (C#)
  │  P/Invoke（cdecl，blittable 结构体，全部走 Task.Run）
  ▼
vms_core.dll  ← 本次要写的「业务门面层」（vms_create/vms_login/vms_get_stats/...）
  │  纯 C，内部调用
  ▼
libqclientcomm (qc_*)  ← 通信内核门面，唯一对外 C ABI
  │  内部封装
  ▼
q_spa / q_chan / q_net / q_codec / q_crypto / q_rand / q_mem  + Tongsuo(libcrypto)
  ▼
网络：UDP SPA 敲门(knockd:5001) → TCP 18099(netd) → UDS(bizd) → 下游微服务 9001/9002/9003/9004
```

**铁律**：C# 绝不直接 P/Invoke `q_chan_*` / `q_net_*` / `q_codec_*` / Tongsuo。理由：
- `q_fd_t` 在 Win64 是 `uint64_t`（承载 SOCKET），`q_chan_t` 是不透明内部结构体，C# 绑定既不安全也不稳定；
- 帧封装（44B 包头 + SM4-GCM + AAD + IV=nonce_base XOR seq）与 seq 窗口由库管理，UI 手工拼包必然出错且是安全事故。

---

## 2. 应该调用的函数清单

### 2.1 通信内核门面（`qc_client.h`，11 个，UI 侧间接使用）

| 阶段 | 函数 | 作用 | 关键参数/约束 |
|---|---|---|---|
| 会话 | `qc_session_create(server_pub_der, len, seq0, key_id)` | 建会话、加载服务端静态公钥 | `seq0=1`、`key_id=1`（必须与 `netd_conf.xml` 一致）；公钥 DER 内置于客户端 |
| 敲门 | **（缺失，需新增 `qc_knock`）** | SPA 单包授权，开防火墙 | 现有库只有 `q_spa_build` + `q_net_socket_udp/addr_resolve/udp_send/close` |
| 连接 | `qc_connect(s, host, port)` | TCP 连接 + SM2 ECDH 握手 | host:port = netd 公网端口 **18099**；内部已设 30s 收发超时 |
| 协商 | `qc_hello(s)` | 握手后第一条应用指令，能力协商 | 未认证态白名单内 |
| 取盐 | `qc_fetch_salt(s, username, salt_out, cap, &len)` | 拉取该账号随机 salt（16B） | 未认证态白名单内；响应 `RESULT + BODY(原始 salt 字节)` |
| 派生 | `qc_pbkdf2_sm3(pass, plen, salt, slen, iter, out32)` | 本地派生 pwd_hash，不触网 | `iter ≥ 100000`；**纯本地** |
| 登录 | `qc_login(s, user, pwd_hash[32], otp, devid, len, cver, token_out[32])` | 双因素登录，拿 32B Token | 体 = `0x0101 USERNAME + 0x0102 PWDHASH + 0x0103 OTP + 0x0104 DEVID + 0x0105 CVER` |
| 业务 | `qc_request(s, cmd, body, blen, resp, cap, &rlen, &cmd_out)` | **所有业务交互的唯一入口** | `cmd ∈ {0x0020 BIZ_QUERY, 0x0021 BIZ_UPDATE, 0x0030 VIDEO_OPEN}`；body 必须以 `0x0106 AUTHTOK(32B)` 开头；严格 lockstep，收发成对 |
| 保活 | `qc_heartbeat(s)` | 保活 | 建议 60s 一次；**注意：netd 本地回 OK，不续期 Token** |
| 注销 | `qc_logout(s)` | 注销并清零 Token | 先 logout 再 close |
| 关闭 | `qc_close(s)` / `qc_session_destroy(s)` | 关连接 / 销毁会话并清零密钥 | destroy 可为 NULL |

### 2.2 SPA 敲门（必须补的一层）

`qc_client.h` 目前**没有敲门能力**，但生产环境 netd 端口被防火墙 filtered，必须先敲门：

```
q_spa_build(spa_key16, 16, key_id, user_id32, req_port=18099, req_ttl, ip_hint16, &pkt)
q_net_socket_udp(&fd) → q_net_addr_resolve(host, 5001, &ss, &len) → q_net_udp_send(fd, &pkt, sizeof, ss, len) → q_net_close(fd)
```

建议：**在 `vms_core.dll` 内封装一个内部函数 `qc_knock(...)`**（或 `vms_knock`），不要把 `q_spa`/`q_net` 暴露到 C#。
参考实现见 `soft_src/knockd/spa_send/main.c`（现有联调发送端，可直接照抄逻辑）。
SPA 密钥 16B：**不要明文落盘、不要所有客户端共用**，建议随账号下发并用 Windows DPAPI (`CryptProtectData`) 或凭据管理器保存。

### 2.3 业务参数怎么传（关键设计点）

`bizd` 的 `body_to_params()` 只认 6 个 tag：`0x0101→username`、`0x0102→pwd_hash`、`0x0103→otp_code`、`0x0104→device_id`、`0x0105→client_ver`、`0x0106→token`；**其它 tag 走 `tag_%04x` 的 hex 兜底**（下游能解但很丑）。

**推荐做法**：在 `include/include_ipc_proto.h` 增加「业务参数段」tag（唯一事实来源），并在 `bizd_handler.c` 的 `field_name_of()` 同步加映射：

| 建议 tag | 名称 | 含义 | bizd 字段名 |
|---|---|---|---|
| 0x0201 | `Q_IPC_T_BIZ_TYPE` | 业务子类型：`stats` / `companies` / `vehicles` / `realtime` / `alarm` | `biz_type` |
| 0x0202 | `Q_IPC_T_BIZ_PARAM` | 子类型参数（推荐 UTF-8 JSON，或嵌套 TLV） | `biz_param` |
| 0x0203 | `Q_IPC_T_PAGE_OFFSET` | 分页偏移 | `offset` |
| 0x0204 | `Q_IPC_T_PAGE_LIMIT` | 每页条数 | `limit` |

请求体固定形状：`0x0106 AUTHTOK(32B) + 0x0201 BIZ_TYPE + [0x0202 PARAM] + [0x0203/0x0204 分页]`。

**响应解析留在 C 层**：bizd 把下游 JSON 原样塞进 `Q_IPC_T_BODY` 回传。
`vms_core.dll` 内用 cJSON（`E:\win_e\cs_dev\cjson` 已就绪）解析，填成 `NativeStats / NativeCompany / NativeVehicle` 数组返回 C#。
→ **C# 侧结构体与解析代码完全不动**，也符合「客户端 TLV-only 不解析 JSON」的既定约定。

---

## 3. UI 能力 → 调用映射

| UI 功能 | `vms_*`（C# 现状，保持不变） | 内部落到 `qc_*` |
|---|---|---|
| 启动自检 | `vms_abi_version` / `vms_struct_size` | 纯本地（可保留） |
| 建会话 | `vms_create(config_json)` | `qc_session_create(内置DER, 1, 1)` |
| 连接 | `vms_connect(endpoint)` | `qc_knock(SPA)` → `qc_connect` |
| 登录（**新增 UI**） | 新增 `vms_login(user, pwd, otp)` | `qc_hello` → `qc_fetch_salt` → `qc_pbkdf2_sm3` → `qc_login` |
| 状态栏统计 | `vms_get_stats` | `qc_request(0x0020, AUTHTOK + type=stats)` → cJSON → `vms_stats` |
| 组织树 | `vms_get_companies` | `qc_request(0x0020, type=companies)` |
| 车辆分页查询 | `vms_query_vehicles` | `qc_request(0x0020, type=vehicles + offset/limit/过滤)` |
| 实时推送 | `vms_subscribe(cb)` | **协议无推送**（见 §4.1）→ native 侧起轮询线程，结果走现有回调 |
| 断开/退出 | `vms_disconnect` / `vms_destroy` | `qc_logout` → `qc_close` → `qc_session_destroy` |
| 错误描述 | `vms_last_error` | 自建错误码表：`0` OK / `>0` 结果码 / `<0` 断连 |

---

## 4. 三条协议硬约束（决定了客户端架构）

### 4.1 严格 lockstep → 现有的「推送」模型必须改
`qc_request` 内部「发一帧、等一帧」，SM4-GCM + seq 窗口不允许流水式多发。
**后台没有任何服务端主动推送通道**，所以 `vms_subscribe` 的语义在真实协议里不成立。

推荐（改动最小）：`vms_subscribe` 内部起一个 native 轮询线程，每 N 秒（建议 2~5s）发一次
`BIZ_QUERY(type=realtime, 携带 last_ts / 已勾选车辆 id 列表)`，把结果通过现有 `vms_push_cb` 回调抛出。
C# 侧的 `_uiQueue.TryEnqueue` 回 UI 线程机制**完全不用改**。

### 4.2 单连接 = 单会话，必须串行化
一个 `qc_session_t` 同时只能有一个未完成请求。C# 侧需要：
- 一个 `SemaphoreSlim(1,1)` 串行化所有 `vms_*` 调用（含心跳）；
- 所有调用包在 `Task.Run`（现有 `NativeVehicleDataSource` 已这么做，保持）。

### 4.3 Token 生命周期
- TTL = `NETD_TOKEN_TTL_SEC` = **1800s**；
- `qc_heartbeat` 只是 netd 本地回 `RESULT=0`，**不续期**（`hnd_heartbeat` 没有重新签发）；
- 因此业务迟早会返回结果码 **3（Token 无效/过期/绑定不匹配）**，客户端必须内置「结果码 3 → 自动 `qc_close` → 重连 → 重新登录」的状态机。

---

## 5. 上线前必须修的 6 个缺口（按阻塞顺序）

| # | 缺口 | 位置 | 后果 | 修法建议 |
|---|---|---|---|---|
| 1 | **登录 Token tag 不一致** | `netd_handler.c:207` 用 `Q_IPC_T_TOKEN(0x0006)` 编码回客户端；`qc_client.c:310` 只认 `Q_IPC_T_AUTHTOK(0x0106)` | 登录必然拿不到 Token（`have_tok=0` → 返回 -1），**永远登不进去** | netd 改用 `0x0106`（与文档/客户端一致，推荐）；或 `qc_login` 同时接受 `0x0006` |
| 2 | **口令校验语义不一致** | `q_s_vmsv1__login/login_handler.c` 把 `params.pwd_hash` hex 解成文本，与配置明文 `123456` 比对 | 客户端按方案发 PBKDF2-SM3(32B) 必然 BADCRED | 登录服务改为按账号存 `pwd_hash(hex)`，与参数做**常量时间**等值比较 |
| 3 | **SALT_QUERY 未实现** | `login_handler.c` 只处理 cmd 3/16/64，cmd=4 走 default → `LOGIN_R_UNSUPPORTED` | `qc_fetch_salt` 失败 | 加 `case Q_CHAN_CMD_SALT_QUERY`，按账号返回 `{"result":0,"data":{"salt":"<32hex=16B>"}}` |
| 4 | **HELLO 无下游** | bizd 路由 `HELLO→ep_hello(9001)`，仓库无该服务；登录服务也不处理 cmd=1 | `qc_hello` 返回非 0 | 让 netd 像 `hnd_heartbeat` 一样本地回 HELLO（改动最小），或补 hello 微服务 |
| 5 | **业务下游缺失** | `ep_biz(9003)` / `ep_video(9004)` 无实现 | BIZ_QUERY 只能返回 `result=1`（下游不可达） | 一期先写 biz 桩服务；客户端保留现有 Mock 降级路径（`VehicleDataSourceFactory` 已具备） |
| 6 | **业务请求 Token 校验链路断裂** | `netd_ipc.c:ipc_build_req` 对业务请求**不发**外层 `TOKEN` TLV；而 `bizd_srv.c:63` 只从外层 `Q_IPC_T_TOKEN` 取 token | bizd 侧 `c->token` 恒为全零，下游拿到的 `ctx.token`/`token` 是 64 个 0 | netd 在 `hnd_biz` 从 body 解出 token 存入 `c->token` 并在 `ipc_build_req` 加 TOKEN TLV；或 bizd 改从 `params.token` 校验 |

> 另注：`netd_hnd_biz` 在下游 `result != OK` 时返回 -1，netd 层可能直接断连 —— 联调时需确认「业务失败」与「连接断开」的边界。

---

## 6. DLL 形态与链接

- **文件名保持 `vms_core.dll`**（`NativeLibraryResolver` 已按此名探测，改名要动 C#）；导出符号保持 `vms_*`，内部实现换成调 `qc_*`。
- 实现依赖（Windows/llvm-mingw 产物已在 `E:\win_e\cs_dev\lib`、`tongsuo`、`cjson`）：

```
libqclientcomm.a libqchan.a libqnet.a libqcodec.a libqcrypto.a libqsec.a
libqrand.a libqmem.a libqutil.a libqlog.a libqreactor.a libqspa.a
+ libcrypto.a libssl.a（Tongsuo）
+ -lws2_32 -lbcrypt -lcrypt32 -lpthread
```
  真静态链接要给 `.a` 全路径（如 `lib/libqchan.a`），否则 MinGW ld 会优先链到同目录的 `libqchan.dll.a`。
  cJSON 解析响应：`-I$(SOFT_HOME)/cjson/include -L$(SOFT_HOME)/cjson/lib -lcjson`。

- **架构**：这批 llvm-mingw 产物是纯 x64（MSVCRT）。**arm64ec 目标不能用**，需另用 MSVC/clang-cl 编一套。
- **内存边界**：谁分配谁释放。`NativeStats/NativeCompany/NativeVehicle` 数组由 C# 分配、C 填充（现状即如此，保持不变）；C 侧返回的任何字符串走 `vms_free()`。
- **线程**：回调（`vms_push_cb`）跑在 native 自建线程，只做数据拷贝后 `DispatcherQueue.TryEnqueue` 回 UI（现有实现正确，保持）。

---

## 7. 落地里程碑建议

1. ✅ **M1 打通登录闭环**（已完成，2026-10-21）：修 §5 的 1/2/3/4 → `qc_knock` 补进 l_4 门面 → 新增三端联调程序 `qc_login_ut` → `vms_core.dll` 新增 `vms_login/logout/heartbeat`。验收见 §7.1。
2. ✅ **M2 打通一条业务查询**（已完成，2026-10-21）：定 §2.3 的业务 tag 段 → 写 biz 桩服务（9003）→ `vms_get_stats` 走通 `qc_request` + cJSON 解析 → C# 状态栏显示真实数据。验收见 §7.2。
3. ✅ **M3 铺开业务面**（已完成，2026-10-22）：`companies` / `vehicles`（分页 + 组织子树过滤 + 关键字 + 主键过滤）按 `sql/vms30.sql` 两表结构返回，data 域用列名的 camelCase；ABI 升至 v4 承载字符串主键 → 替换 Mock 数据源。接口见《biz微服务接口约定.md》，验收见 §7.3。
4. **M4 会话治理**：心跳线程（60s）+ 结果码 3 自动重登 + 串行化 gate + 断线重连退避；并把环境变量自动登录换成正式登录对话框。
5. **M5 实时**：`vms_subscribe` 改为 native 轮询线程（2~5s），保留现有回调与 UI 线程切换。
6. **M6 视频**：`VIDEO_OPEN(0x0030)`，按技术方案走独立连接，另立子任务。

---

### 7.1 M1 验收记录（2026-10-21，Windows + Linux 双端实跑）

**改了什么**

| 位置 | 改动 |
|---|---|
| `soft_src/netd/netd_handler.c` | ① 登录 Token 改用 `Q_IPC_T_AUTHTOK(0x0106)` 回客户端（`0x0006` 保留为 netd↔bizd wire tag）；② `hnd_biz` 业务失败**不再断连**（原 `result!=OK` 返回 -1 → 客户端只能看到 -1 拿不到结果码）；③ 抽出 `netd_put_result()` 统一编 RESULT |
| `soft_src/q_s_vmsv1__login/*` | ① 新增 `SALT_QUERY(cmd=4)` 分支，按账号下发 salt+iter；② 口令改为与本地 PBKDF2-SM3 期望值做**常量时间**比较（`param_bytes()` 直取 32B 原始字节，不再当文本）；③ 新增 `-S/-K/-i/-O` 与 `LOGIN_SALT/LOGIN_PWD_HASH/LOGIN_PBKDF2_ITER/LOGIN_OTP`；④ 可选 OTP 校验 |
| `soft_src/bizd/bizd_handler.c` | `bizd_hnd_salt` 所有出口都回带 RESULT（原先下游没给 salt 时回空响应 → 客户端卡到 30s 超时） |
| `include/include_ipc_proto.h` | 新增 `Q_PBKDF2_ITER_DEFAULT(100000)/SALT_LEN(16)/OUT_LEN(32)` —— 客户端与登录服务共用同一事实来源，杜绝协议漂移 |
| `include/include_xml.h` | 删除重复的 `<cjson/cJSON.h>`（Windows shim 遗留，Linux/macOS 上直接编译失败） |
| `lib_src/l_4/q_clientcomm` | 新增 `qc_knock()`（SPA 敲门，UDP 单包，三端通用）；`qc_login` 兼容 `0x0006`/`0x0106` 两种 Token tag |
| `include/Makefile.inc` | 新增 `DEP_HOME ?= $(SOFT_HOME)`（三端依赖根，交叉编译可覆盖，避免 Windows/Linux 产物互相覆盖） |
| 各模块 Makefile | `bizd/netd/login/knockd/q_xcfg` 的依赖路径改走 `DEP_HOME`（并修正 `cJSON`→`cjson` 大小写漂移） |
| `lib_src/l_4/.../ut/qc_login_ut.c` | **新增**：登录闭环联调程序（敲门→握手→HELLO→SALT→PBKDF2→LOGIN→Token→心跳→登出），三端同一份源码 |
| `shell/build_login_ut_win.sh` | **新增**：Windows 侧编译联调程序；`shell/build_vmscore_win.sh` **新增**：编译接入 `qc_*` 的 `vms_core.dll` |
| `win_vms_v1/native` + C# Interop | `vms_api.h` ABI 升至 v3，新增 `vms_credentials` + `vms_login/logout/heartbeat`；C# 侧同步（`ExpectedAbiVersion=3`、结构自检加 Credentials、`VmsCoreClient.Login/Logout/Heartbeat`） |

**实测结果（WSL Ubuntu 起 netd+bizd+login，客户端分别在 Windows / Linux 跑）**

```
[Linux  ] qc_login_ut:  握手 OK / HELLO rc=0 / salt=0011…eff / pbkdf2 iter=100000 31ms
                        / Token(32B)=4d6f76ff… / heartbeat rc=0 / logout rc=0  → ALL PASS
[Windows] qc_login_ut:  同上，Token=f70b4759…（含 --knock 路径亦通过）          → ALL PASS
[Windows] vms_core.dll: abi=3 struct_cred=872(一致) / vms_login=0 / hb=0 / logout=0 → OK
两端派生出的 pwd_hash 完全一致（0c01f206…），证明三端口令派生与服务端校验对齐。
```

**复现命令**

```bash
# Linux/macOS
make -C lib_src/l_4/q_clientcomm ut_login [LIB=... DEP_HOME=... TONGSUO_HOME=...]
./lib_src/l_4/q_clientcomm/ut/bin/qc_login_ut --host 127.0.0.1 --port 18099 \
    --user qiaoshui --pass 123456 --pub <netd 的 netd_sm2.pub>

# Windows
bash shell/build_login_ut_win.sh
ut/bin/qc_login_ut.exe --host <ip> --port 18099 --user qiaoshui --pass 123456 \
    --pub <netd_sm2.pub> [--knock <ip>:5001 --spakey <16B 密钥文件>]
```

**已知未覆盖**：macOS 侧无 darwin 工具链，仅做编译性保证（源码无平台专属分支）；
`knockd` 在 WSL 下启用 IPv6 监听后 segfault（疑似 nft 上下文初始化失败后未判空，既有缺陷，
与本次改动无关），故 SPA **服务端侧**校验留待真实 Linux + nftables 环境验证。

---

### 7.2 M2 验收记录（2026-10-21，Windows + Linux 双端实跑）

**目标**：打通一条真实业务查询 —— `vms_get_stats` 从「Mock」变成「后台返回的真实数字」。

**改了什么**

| 位置 | 改动 |
|---|---|
| `include/include_ipc_proto.h` | ① 新增**业务参数段** tag：`0x0201 BIZ_TYPE`(文本) / `0x0202 BIZ_PARAM`(文本) / `0x0203 PAGE_OFFSET`(u32 BE) / `0x0204 PAGE_LIMIT`(u32 BE)，并在注释里钉死"承载类型"（文本 vs 数值 vs hex）；② 新增结果码 `Q_IPC_R_UNSUPPORTED=6`（下游回"子类型未实现"） |
| `soft_src/bizd/bizd_handler.c` | `field_name_of()` 加 4 个映射；`body_to_params()` 按 tag 语义分派承载方式（文本原样 / u32 数值 / 其余 hex）。**文本型拒绝内嵌 NUL**（cJSON 按 strlen 取串，内嵌 NUL 会造成"截断欺骗"） |
| `soft_src/netd/netd_ipc.c` + `srv_netd.h` + `netd_handler.c` | **补掉 §5 缺口 6**：`ipc_build_req()/ipc_forward()` 新增 `token` 参数，`hnd_biz` 在已登录时把会话 Token 装进外层 `Q_IPC_T_TOKEN`（LOGIN 阶段显式传 NULL）。不修则 bizd 侧 token 恒全零 —— 业务鉴权静默失效 |
| `soft_src/q_s_vmsv1__biz/`（**新增**） | biz 桩服务（9003）：reactor + 线程池 + JSON 信封，支持 `BIZ_QUERY(cmd=32)` + `biz_type=stats`；Token 桩校验（非空且非全零）；`--total/--online/--alarm` 可改基线用于证明数据来源 |
| `lib_src/l_4/q_clientcomm` | 新增 `qc_biz_query()`：统一拼 `AUTHTOK + BIZ_TYPE + [BIZ_PARAM] + [OFFSET/LIMIT]` 再调 `qc_request(0x0020)`，杜绝三端各自手拼 TLV 造成协议漂移 |
| `soft_src/win_vms_v1/native` | `vms_get_stats` 改为：**已登录** → `qc_biz_query("stats")` + cJSON 解析下游 JSON 的 `result/data`；失败**不回退 Mock**（宁可报错也不给运维看假数字）。`vms_api.h` 相应把形参 const 去掉并补充语义说明 |
| `shell/build_vmscore_win.sh` | 链入 `cjson/lib/libcjson.a` 并加 `-Icjson/include`（解析下游 JSON 用；**不走** `include_xml.h`，后者依赖 libxml2） |
| `ut/qc_login_ut.c` | 新增 `--biz-type / --biz-param / --no-biz`，登录后默认发一次 BIZ_QUERY 并打印下游 JSON 原文（TLV-only：只打印不解析） |
| C# 侧 | `VehicleDataSourceFactory.TryCreateLoginOptionsFromEnvironment()`（VMS_HOST/USER/PASS/PUBKEY…）；`NativeVehicleDataSource.LoginAsync()`（**必须** Task.Run：native 阻塞式网络调用）；`MainViewModel.TryAutoLoginAsync()` 在启动流程里先登录再刷统计 |

**实测结果**（WSL 起 login/bizd/netd/biz 四进程，客户端分别在 Windows / Linux 跑）

```
[Linux  ] qc_login_ut:  … Token=c5feda67… -> qc_biz_query rc=0
          下游响应: {"result":0,"data":{"total":40000,"online":12345,"alarm_unhandled":777,…}}  ALL PASS
[Windows] qc_login_ut:  同上，Token=30d88863…                                                  ALL PASS
[Windows] vms_core.dll: [mock] 34554/10846/46343  ->  [real] 40000/12345/777  rate 30.86        OK
```

**三条关键证据**

1. **biz 服务收到的 `token` = 登录签发的 Token**（`c5feda67…`）—— 证明缺口 6 已修；不修的话这里恒为 64 个 0，会被 UNAUTHORIZED 拒掉。
2. **bizd 转出的 `params` 是 `{"biz_type":"stats"}`（文本，不是 hex）** —— 证明新 tag 段与承载类型生效。
3. **mock(34554) 与真实(40000) 数字不同** —— 证明状态栏数字确实来自服务端，而不是本地造的。

**复现命令**

```bash
# 服务端（Linux）
bin/q_service_login  -p 9002 -u qiaoshui -k 123456 -S <32 hex salt>
GTKVMS_BIZD_CONF=bizd_conf.xml  bin/q_service_bizd          # ep_biz=127.0.0.1:9003
GTKVMS_NETD_CONF=netd_conf.xml  bin/q_service_netd          # 18099
bin/q_service_biz -p 9003 --total 40000 --online 12345 --alarm 777   # M2 桩服务

# 客户端
./bin/qc_login_ut --host 127.0.0.1 --port 18099 --user qiaoshui --pass 123456 --pub <netd_sm2.pub>
# Windows 侧另可：vms_biz_test.exe 127.0.0.1 18099 qiaoshui 123456 netd_sm2.pub

# C# 客户端（M2 自动登录通道）
set VMS_HOST=127.0.0.1 & set VMS_PORT=18099 & set VMS_USER=qiaoshui
& set VMS_PASS=123456 & set VMS_PUBKEY=<netd_sm2.pub>
```

**已知未覆盖 / 下一步**
- `companies` / `vehicles` 子类型仍返回 `Q_IPC_R_UNSUPPORTED(6)` —— 归 M3。
- biz 桩服务的 Token 校验只做"非空且非全零"；真实部署要查会话存储并校验 `(token, channel_id, user_id, src_ip)` 四元绑定。
- macOS 仍无 darwin 工具链，只做编译性保证（本次改动无平台专属分支）。
- C# 自动登录走环境变量只是**联调通道**，生产必须换登录框 + 凭据管理器（M3/M4）。

---

### 7.3 M3 验收记录（2026-10-22，Windows + Linux 双端实跑）

**目标**：铺开业务面 —— `vms_get_companies` / `vms_query_vehicles` 从「本地 Mock」换成
「后台按 `sql/vms30.sql` 两张表结构返回的真实数据」。

**接口事实来源**：《biz微服务接口约定.md》（请求/响应信封、字段与库表列对照、分页与过滤规则）。
data 域字段一律是**库表列名的 camelCase**（`vehicle_id` → `vehicleId`，`note` → `note`）。

**改了什么**

| 位置 | 改动 |
|---|---|
| `soft_src/q_s_vmsv1__biz/biz_stub.c`（**新增**） | 桩数据生成：`d_org_info` 25 个组织 + `d_vehicleinfo` 34554 台车。**确定性**（行内容只由行号决定）→ 翻页不重不漏；只生成 `[offset, offset+limit)` 这一段，`total` 恒回过滤后的总行数 |
| `soft_src/q_s_vmsv1__biz/biz_handler.c` | 新增 `handle_companies` / `handle_vehicles`：`biz_param`（JSON 文本）二次解析 → 组织子树/关键字/在线/报警/主键过滤 → 统一列表信封 `{total, offset, limit, items}` |
| `soft_src/q_s_vmsv1__biz/srv_biz.h` + `main.c` | `biz_veh_filter_t`；`--vtotal/--vonline/--page-max`；单页上限 **100**（一条车辆 JSON 约 480B，再大就撞上 `Q_IPC_TLV_MAX=60000`，响应被 bizd 截断成非法 JSON） |
| `win_vms_v1/native/include/vms_api.h` | **ABI 升至 v4**：`vms_company` 追加 `org_id`、`vms_vehicle` 追加 `vehicle_id` + `org_id`。原因：`org_id` CHAR(36) / `vehicle_id` CHAR(32) 是**字符串主键**，uint32 装不下（真实系统可能是 UUID 或"车牌+颜色"） |
| `win_vms_v1/native/src/vms_core.c` | 新增 `vms_fetch_companies()` / `vms_fetch_vehicles()`：JSON → 结构体搬运；`company_id`(uint32) → `org_id`(字符串) 的翻译查本机组织树缓存；`VMS_MAX_COMPANY` 64 → 512 |
| C# 侧 | `NativeCompany.OrgId` / `NativeVehicle.VehicleId+OrgId`；`CompanyRecord.OrgId`、`VehicleRow.VehicleId/OrgId`；`ExpectedAbiVersion=4`；`dotnet build` 0 警告 0 错误 |
| `shell/biz_probe.py`（**新增**） | 裸协议探针：绕过 netd/bizd 直连 9003，`--selftest` 校验 total 期望值与分页连续性 |
| `shell/e2e_m3.sh`（**新增**） | 全链路脚本：起 4 个服务 → 跑 stats/companies/vehicles 全部用例 → 收尾 |
| `shell/vmscore_smoke.py`（**新增**） | Windows 侧 ctypes 直连 `vms_core.dll`：校验 ABI v4 结构体尺寸 + 打印组织树与车辆行 |
| `ut/qc_login_ut.c` | 新增 `--offset/--limit`；响应缓冲 8KB → 64KB（列表 JSON 可达几十 KB） |

**实测结果**

```
[biz 自检]  companies 全量 25 / parent=1 直属 18 / parent=14 子树 3            ALL OK
            vehicles 全量 34554 / org=14 子树 398 / org=11 子树 470
            / org=14 非递归 0 / online_only 10846 / ids 精确过滤 2
            分页连续性 page0=[000001..000005] page1=[000006..000010] 无重叠   ALL OK
[全链路 ]  qc_login_ut: stats / companies / vehicles 共 7 个用例            ALL PASS
[Windows]  vms_core.dll: ABI=4  struct: company 128B / vehicle 312B / query 1120B（与 DLL 一致）
           vms_get_companies -> 25 个组织（id=1 orgId=1 运输公司 8960/18714 下级=18 …）
           vms_query_vehicles(全部) -> 命中 34554，本页 vehicleId=000001 京Z38406 orgId=2 …
           vms_query_vehicles(org=14) -> 命中 398，本页 vehicleId=008723 orgId=1401 …  → OK
```

**三条关键证据**

1. **组织子树计数自洽**：`org=14` 返回 398 台 = 1401(210) + 1402(188)；`recursive=0` 返回 0 台 ——
   证明"车辆挂在叶子单位、按组织过滤 = 含所有下级"这条产品语义在服务端落地了。
2. **字符串主键真的搬到了结构体**：Windows 侧 `vehicleId=008723 / orgId=1401` 与服务端 JSON 完全一致，
   说明 ABI v4 新增字段与 cJSON 映射都正确（字段顺序写错这里会立刻乱码/崩溃）。
3. **分页不重不漏**：`page0=[000001..000005]`、`page1=[000006..000010]` 无交集且严格递增 ——
   确定性生成保证了 UI 增量加载不会重复/丢行。

**复现命令**

```bash
# 服务端裸测（最快：不启 netd/bizd）
bin/q_service_biz --no-token -p 9003 &
python3 shell/biz_probe.py --selftest
python3 shell/biz_probe.py --type vehicles --param '{"org_id":"14","recursive":1}' --limit 3

# 全链路
bash shell/e2e_m3.sh                       # Linux/WSL：起 4 个服务 + 跑全部用例
bash shell/e2e_m3.sh ONLY=vehicles BIZ_PARAM='{"org_id":"14"}' LIMIT=5

# Windows 侧（需服务已在跑；公钥要放到 Windows 可访问的路径）
python shell/vmscore_smoke.py --pub E:\win_e\cs_dev\tmp_e2e\netd_sm2.pub --limit 5
```

**已知未覆盖 / 下一步**
- **Ids 过滤仍走 uint32**：`vms_query.ids[]` 按 `"%06u"` 还原成桩主键；真实系统 `vehicle_id` 非数字时
  应改传字符串数组（ABI v5 待办）。`company_id` → `org_id` 的翻译依赖本机组织树缓存，未加载时会先拉一次。
- 桩服务**不校验** `org_id` 是否真实存在（未知 org 返回 0 行，不报错）。
- biz 桩未做聚合校验：`vehicleCount` 取截图值（18714 等），与车辆实际归属数（9392）不严格相等 —— 换真实 SQL 时应统一。
- C# 仍是环境变量自动登录通道；M4 应换正式登录对话框 + 凭据管理，并接 M4 的心跳/重连状态机。

---

## 8. 一句话总结

> **该调的函数就是 `qc_client.h` 那 11 个 `qc_*`（外加一个待补的 SPA 敲门 `qc_knock`）**；
> C# 侧不动，把 `vms_core.dll` 从「造数据」改成「调 `qc_*` + cJSON 翻译」；
> 真正的工作量不在客户端绑定，而在 §5 那 6 个服务端/协议缺口 —— 不修，客户端连登录都过不去。

---

# 附录 A：三端统一影响评估（Windows 适配改动会不会伤到 Linux / macOS）

## A.1 逐条结论：库层源码对 POSIX 零影响

| 改动 | 影响面 | 结论 |
|---|---|---|
| `include_stdio.h` 前置 `winsock2.h` | `#ifdef Q_SYS_WINDOWS` | 零 |
| `include_net.h` EPOLL*/MSG_NOSIGNAL/AF_UNIX/htobe64 宏 | Windows 分支 | 零 |
| `Q_WINSOCK_INIT()` / `Q_SOCKET()` | POSIX 下 `Q_SOCKET()` = 原生 `socket()`；`Q_WINSOCK_INIT()` = `((void)0)` | 零（见 A.2 修复项 1） |
| 13 处 `socket(AF_*` → `Q_SOCKET(AF_*` | 展开等价 | 零 |
| `include_thd.h` `q_mutex_t` → `pthread_mutex_t` | `#ifdef Q_SYS_WINDOWS` | 零 |
| `q_rand` 改 BCryptGenRandom | Windows 分支，POSIX 仍 `/dev/urandom` | 零 |
| `q_common_seccomp.c` | 已改 `#ifdef Q_SYS_LINUX`（原写法 macOS 会挂，已修） | 零，且 macOS 从"编译不过"变"可编译" |
| `q_common_harden.c` / `q_common_signal.c` | Windows 分支；macOS 走原 POSIX 路径（chroot/setuid/sigaction 均可用） | 零 |
| `q_mem/headers.h` 加 `<windows.h>` | Windows 分支 | 零 |
| `q_evsrc_poll_*`（tick 签名 void→int） | **Windows 专用后端**（Makefile：Linux=epoll、macOS=kqueue、Windows=poll） | 零 |
| `q_spa` 去掉 `<arpa/inet.h>` | 改由 headers.h 引入，POSIX 头文件链不变 | 零 |

**WSL gcc 全量回归**：`make -j4` → exit 0，无编译告警（仅剩既有的 `jobserver unavailable` make 提示）。

## A.2 已修复的 3 个真问题

1. `Q_WINSOCK_INIT()` 在非 Windows 展开为 `0`，调用点是语句形式 → **gcc/clang 报 `-Wunused-value`**（属 `-Wall`）。已改为 `((void)0)`。
2. `q_common_seccomp.c` 用 `#ifndef Q_SYS_WINDOWS` 包 `<seccomp.h>` → **macOS 仍会 include 失败**。已改 `#ifdef Q_SYS_LINUX`。
3. `q_mem/headers.h` 我的注释里含 `api/*.c` → 嵌套 `/*` 触发 `-Wcomment`。已改文案。

## A.3 ⚠️ 真正会伤到三端的是"构建产物目录"，必须先改

`include/Makefile.inc` 的 `clean:` 会执行
`rm -f $(LIB)/$(TARGET) $(LIB)/$(TARGET_SO) $(INCLUDE)/$(HEADER)`，
而 `LIB`/`INCLUDE` 指向**源码树里的 `E:/win_e/cs_dev/lib` 与 `include/`**。
后果（今天已真实发生）：在 Windows 上跑 WSL 的 `make clean` →
① 删掉了刚编好的 Windows 版 `libq*.a`；② **删掉了 git 跟踪的 `include/*.h`**（14 个，已 `git checkout --` 恢复）。

**建议**：产物按三元组分目录，`lib/x86_64-w64-mingw32/`、`lib/x86_64-linux-gnu/`、`lib/arm64-darwin/`，
或给 Makefile.inc 增加平台后缀变量。否则三端产物互相覆盖是必然事故。

## A.4 macOS 待实测点（本机无 darwin 工具链，只能人工核对头文件分支）

- `q_reactor` 的 kqueue 后端（`api/q_evsrc_kqueue_*`）未经任何编译验证；
- `include_net.h` macOS 分支的 `OSSwap*` 系列字节序宏；
- `include_time.h` macOS 时钟（mach_absolute_time / clock_gettime 可用性）；
- `q_common` 现在**新增可编译**（seccomp 桩），但 `daemon_harden` 在 macOS 走 chroot/setuid 分支需实测。

---

# 附录 B：多线程 / 长连接 / 心跳 / 重连 —— 可行性评估

## B.1 结论速览

| 设想 | 可行性 | 说明 |
|---|---|---|
| 定位数据单独线程 | ✅ 可行 | 但只能是**轮询**，不是"服务端推送" |
| 定位数据单独**长连接** | ⚠️ 可行但需服务端配合 | 协议严格 lockstep；且新连接要重新认证，会撞 OTP 防重放（B.2） |
| 心跳机制 | ✅ 可行 | 注意 netd 心跳**不续期 Token** |
| 自动重连 | ✅ 可行 | 但"静默重登"必须引入 refresh/ticket 机制，否则要用户重输口令+OTP |
| 流媒体单独线程 + 长连接 | ✅ 架构上正确，❌ 服务端未实现 | 技术方案 §9 已定"视频独立连接"；但 netd 现在没有流式下发 |
| 三条连接各登录一次 | ❌ 不可行 | 撞 TOTP `last_used_step` 防重放 |

## B.2 决定架构的 5 条硬约束

1. **一连接 = 一会话**：Token 绑定 `channel_id(=SM3(会话密钥)) + user_id + device_id + src_ip`。跨连接复用 Token 语义上就是"绑定不匹配" → 结果码 3。
2. **OTP 防重放**（方案 §6.1）：服务端记录 `last_used_step`，只允许 `step > last_used_step`。→ **同一 30s 窗口内第二次登录必被拒**。这是"多连接各登录一次"的直接杀手。
3. **新建连接限流**：`NETD_PERIP_NEW = 5 次/秒/单 IP` → 重连风暴必须退避，退避下限 ≥ 1s。
4. **心跳不续期**：`netd hnd_heartbeat` 只本地回 `RESULT=0`，不重签 Token（TTL 1800s）→ 到期后业务必返回 3，必须重登。
5. **重登凭据**：pwd_hash 不应驻留内存/落盘、OTP 一次性 → 无 refresh 机制时自动重连无法静默完成。

## B.3 推荐路线

### 一期（零服务端新增协议改动）：单连接 + 单 IO 线程
- 整个客户端 **1 个 `qc_session_t` + 1 个 IO 线程**，内部请求队列 + 优先级：
  定位轮询（2~5s）为常态流量，UI 查询插队优先，心跳仅在空闲 > N 秒时发。
- 理由：lockstep 下串行本就无并发收益；定位一次请求几十 ms 内返回，2s 轮询占用率 <5%，完全够用。
- 好处：OTP 只消耗一次、不受 5/s 新建连接限流、无需服务端改协议。
- 风险与兜底：单次大查询会推迟下一次轮询 → 限制单帧业务体（≤64KB）+ 分页，必要时查询分片。

### 二期（真多连接）：先补"会话票据"，再谈多连接
- 新增指令（建议 `0x0005 SESSION_RESUME`）：控制连接登录后签发**短期一次性 ticket**
  （服务端 SM2 签名或 HMAC，绑定 `user_id + device_id + src_ip`，TTL 60s，用后即废）；
  新连接握手后凭 ticket + 本通道 `channel_id` 换取本通道 Token。
- 这样 OTP 只输一次，每条连接仍有独立 Token 且仍绑定通道，安全语义不降。
- 横向扩容时 ticket 需 Redis 共享（与《扩容方案》里的 Redis 计划一致）。
- **不推荐**的省力做法：放宽 `last_used_step` 或去掉 `channel_id` 绑定 —— 直接削弱防重放/通道绑定，等保三级 + 密评不好交代。

### 视频连接（二期）
- 技术方案 §9 已定：视频走**独立 TCP 连接**，独立限流/背压，允许丢帧，不参与时间窗 —— 架构预留是对的。
- 库层可行性已核实：`q_chan_recv` 的 seq 滑动窗口接受 `(last, last+64]` 单调递增，
  **服务端单向连续推送在密码学层是兼容的**（时间戳窗 ±300s 也够）。
- **但有两个坑**：
  1. "允许丢帧"指应用层容忍，**seq 不能断号 > 64**，否则 `q_sec` 窗口判异常 → 整条视频连接报废。
     视频服务必须"帧可丢、seq 连续分配"，或给视频通道单独放宽窗口（要改 `q_chan`）。
  2. netd 当前 worker 模型只有一问一答，**没有任何流式下发路径**，需新增视频服务的推帧逻辑。
- 认证方案二选一：**(a)** 复用上面的 ticket（体验好）；**(b)** 把"开视频"做成**高敏操作二次认证**（重输 OTP / 二期 UKey）
  —— 我更倾向 (b)：开视频是低频动作，且"重要操作再认证"是等保三级的加分项。

## B.4 心跳 + 重连状态机（建议做进 l_4 门面，不放 UI）

```
DISCONNECTED
  └─(SPA 敲门；若开门 TTL 已过期则先敲门)─> CONNECTING ──握手──> HELLO
        └─> AUTH(登录 / ticket 换 Token) ──> READY ──(空闲心跳 / 业务 / 轮询)──┐
                                                                              │
        断连 / 结果码 3 / 超时 ──> BACKOFF(1→2→4→…→30s，带抖动，且 ≥ NETD_PERIP_NEW 限制) ──┘
```
建议新增门面能力（现在全是空白）：
`qc_knock()`、`qc_login_with_ticket()`、`qc_set_keepalive(idle, interval)`、
`qc_session_set_callbacks(on_state, on_push)`、`qc_stream_recv()`（视频只收不发）。
**IO 线程由门面内部起**，对外只给 C ABI + 回调，三端 UI 各自只负责把回调切回 UI 线程
（WinUI3 `DispatcherQueue` / GTK4 `g_idle_add` / macOS `dispatch_get_main_queue`）。

## B.5 线程安全核实

- `qc_session_t` **非线程安全**：一个 session 只能由一个线程操作（lockstep 决定了这点）。
- **多 session + 多线程是安全的**：`q_log` 内部有 mutex（已核对 `q_log_write.c` 等全部加锁）、
  `q_rand` 无全局状态（POSIX 每次 open `/dev/urandom`，Windows BCrypt 无状态）、
  Tongsuo/OpenSSL 3 线程安全、`q_mem` / `q_ds` / `q_codec` / `q_sec` 均无共享全局。
- 小瑕疵：`Q_WINSOCK_INIT()` 是 `static inline` + 每 TU 一份 static 标志 → 多 TU 各调一次
  `WSAStartup`（引用计数，无害但冗余）。若要干净，可改为 `q_net` 导出的 `q_net_winsock_init()`。
