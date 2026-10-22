# biz 微服务接口约定（companies / vehicles）

> 适用范围：`bizd` 的下游业务微服务（`soft_src/q_s_vmsv1__biz`，默认 9003）。
> 事实来源：本文件 + `include/include_ipc_proto.h`。**字段命名/承载类型以本文为准，
> 一端改动即协议漂移，改前先改这里。**
>
> M3 交付：组织机构（d_org_info）与车辆（d_vehicleinfo）两个列表接口，
> 桩实现不连库（确定性生成），真实部署把 `biz_stub.c` 换成 SQL 查询即可，
> 上层 `biz_handler.c` 与客户端解析代码一律不动。

---

## 1. 信封格式（与 include_ipc_proto.h 完全一致）

### 请求 bizd -> 微服务

```json
{
  "ver": 1,
  "id":  "17",
  "cmd": 32,
  "cmd_name": "BIZ_QUERY",
  "ctx": { "src_ip": "127.0.0.1", "user_id": "<64hex>",
           "device_id": "<64hex>", "channel_id": "<64hex>" },
  "token": "<64hex>",
  "params": {
    "token":     "<64hex>",
    "biz_type":  "companies",
    "biz_param": "{\"org_id\":\"14\",\"recursive\":1}",
    "offset":    0,
    "limit":     50
  }
}
```

* `params.biz_type` —— 业务子类型，来自客户端 TLV `0x0201`（文本）。
* `params.biz_param` —— **JSON 文本**（不是对象），来自 TLV `0x0202`（UTF-8 文本）。
  微服务侧需二次 `cJSON_Parse`；非法 JSON 返回 `result=2 BADREQ`。
* `params.offset / params.limit` —— 来自 TLV `0x0203 / 0x0204`（uint32 大端，bizd 转成 JSON number）。
  **`limit` 缺省或 0 时由服务端取默认页大小。**
* `token` —— 32B 会话 Token 的 64 hex；bizd 从外层 `0x0006` 透传（M1 缺口 6 已修）。

### 响应 微服务 -> bizd

```json
{
  "ver": 1,
  "id": "17",
  "result": 0,
  "message": "ok",
  "data": { "total": 398, "offset": 0, "limit": 50, "items": [ ... ] }
}
```

* `result` 必须用 `include_ipc_proto.h` 的 `Q_IPC_R_*`：
  0 OK / 1 UNAVAILABLE / 2 BADREQ / 3 UNAUTHORIZED / 4 FORBIDDEN / 5 INTERNAL / 6 UNSUPPORTED。
* 列表类响应的 `data` 固定四字段：`total`（过滤后总行数）、`offset`、`limit`、`items`。

---

## 2. 通用约定

| 项 | 约定 |
|---|---|
| 字段命名 | **库表列名的 camelCase**：去下划线、下划线后首字母大写（`vehicle_id` → `vehicleId`，`wgs84_lon` → `wgs84Lon`，`note` → `note`） |
| 字符串列 | JSON string；Oracle `CHAR(n)` 定长补空格，**取值后必须 RTRIM** |
| 数值列 | JSON number（`NUMBER(1)`/`NUMBER(12,6)` 都按数字，不转字符串） |
| 日期列 | JSON string，`YYYY-MM-DD HH24:MI:SS` |
| 可空列 | 无值一律 **JSON `null`**，不是空字符串 |
| 扩展字段 | 非库表列（见 §3/§4 表尾），真实部署由定位表 JOIN / 统计缓存提供，**形状必须保持一致** |
| 单页上限 | **100 条**。一条车辆 JSON 约 480B，超过会撞上 `Q_IPC_TLV_MAX=60000`，响应被 bizd 截断成非法 JSON。默认页 50 |
| 确定性 | 桩实现的行内容只由行号决定，同一 `(offset,limit)` 结果完全一致（翻页不重不漏） |

---

## 3. `biz_type = "companies"`（d_org_info）

### 请求 biz_param

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `parent_org_id` | string | 空 = 全部 | 按父组织过滤；根节点约定为 `-1` |
| `recursive` | int | 1 | 1 = 返回整棵子树；0 = 只返回直接子级 |

### 响应 data.items[]

| JSON 字段 | 库表列 | 类型 | 说明 |
|---|---|---|---|
| `orgId` | org_id | string | CHAR(36) 主键 |
| `parentOrgId` | parent_org_id | string | 根节点为 `-1` |
| `orgLongName` | org_long_name | string | |
| `orgShortName` | org_short_name | string | UI 显示用简称 |
| `orgAddress` | org_address | string | |
| `wgs84Lon` / `wgs84Lat` | wgs84_lon / wgs84_lat | number | 可空 → null |
| `hyLon` / `hyLat` | hy_lon / hy_lat | number | 华油偏转坐标 |
| `orgRanking` | org_ranking | string | CHAR(4) |
| `orgShowId` | org_show_id | string | 0 不展示 / 1 展示 |
| `orgTelephone` | org_telephone | string | |
| `orgPhone` | org_phone | string | |
| `contactPerson` | contact_person | string | |
| `deleteFlag` | delete_flag | string | 0 生效 / 1 失效 |
| `beginTime` / `endTime` | begin_time / end_time | string | |
| `createTime` / `createBy` | create_time / create_by | string | |
| `modifyTime` / `modifyBy` | modify_time / modify_by | string/null | |
| `note` | note | string/null | |
| `vehicleCount` | **扩展** | number | 车辆数（真实由统计表算） |
| `onlineCount` | **扩展** | number | 在线数 |
| `childCount` | **扩展** | number | 直接下级数（由 parent 关系算） |

---

## 4. `biz_type = "vehicles"`（d_vehicleinfo）

### 请求 biz_param

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `org_id` | string | 空 = 全部 | 按归属组织过滤（字符串主键，不做 atoi） |
| `recursive` | int | 1 | 1 = 该组织及其所有下级（**子树语义，产品默认**）；0 = 仅本级直属 |
| `keyword` | string | 空 | 车牌 / 自编号模糊匹配 |
| `online_only` | int | 0 | |
| `alarm_only` | int | 0 | 配合 `alarm_mask` 位与 |
| `alarm_mask` | int | 0 | 0 表示"任一报警" |
| `ids` | string[] | 空 | 车辆**主键字符串**精确过滤（树上勾选若干车辆），上限 128。元素必须是 `vehicleId` 原值，**禁止**做任何数值转换 |

### 响应 data.items[]

| JSON 字段 | 库表列 | 类型 | 说明 |
|---|---|---|---|
| `vehicleId` | vehicle_id | string | CHAR(32) 主键。库表注释原文：**「车辆ID，车牌号+颜色 最后整-V的为外单位导入车辆」**，即 `京A12345-1` / `京A12345-V` 这种**非数字**业务主键 |
| `lisPlate` | lis_plate | string | CHAR(20)，需 RTRIM |
| `lisPlateColor` | lis_plate_color | string | 1蓝 2黄 3白 4黑 5绿 |
| `orgId` | org_id | string | 归属组织 |
| `mDriverEmpid` | m_driver_empid | string | 主驾 |
| `sDriverEmpid` | s_driver_empid | string/null | 副驾 |
| `supEmpid` | sup_empid | string/null | 押运员 |
| `vehicleTypeId` | vehicle_type_id | string | |
| `vehicleUse` | vehicle_use | string | 字典项 |
| `scrappedFlag` | scrapped_flag | number | 0/1（NUMBER(1) → 数字） |
| `erpCode` | erp_code | string | |
| `userDefinedCode` | user_defined_code | string | UI 的"自编号" |
| `dbhKeyId` | dbh_key_id | string/null | |
| `dataFrom` | data_from | string | V 车辆终端 / F 地区平台 |
| `createTime` / `createBy` | create_time / create_by | string | |
| `modifyTime` / `modifyBy` | modify_time / modify_by | string/null | |
| `note` | note | string/null | |
| `online` | **扩展** | number | 0/1，来自定位表 |
| `offlineSeconds` | **扩展** | number | 离线时长 |
| `alarmFlags` | **扩展** | number | 位掩码，与 `vms_api.h` 的 `VMS_ALARM_*` 对齐 |
| `alarmText` | **扩展** | string/null | 报警类型名 |
| `speedKph` | **扩展** | number | |
| `lon` / `lat` | **扩展** | number | GCJ-02 |
| `lastTime` | **扩展** | string | 最后定位时间 |
| `location` | **扩展** | string | 位置描述 |

---

## 5. 客户端侧映射（vms_core.dll，ABI v4）

| vms_* 结构体字段 | 来源 |
|---|---|
| `vms_company.id` | `orgId` 为纯数字时转换，否则用行号兜底 |
| `vms_company.org_id` | `orgId`（字符串主键，**真实系统检索用它**） |
| `vms_company.parent_id` | `parentOrgId`（`-1` → 0） |
| `vms_company.name` | `orgShortName` 优先，空则 `orgLongName` |
| `vms_company.count / online / child_count` | `vehicleCount / onlineCount / childCount` |
| `vms_vehicle.id` | `vehicleId` 纯数字时转换，否则行号兜底 |
| `vms_vehicle.vehicle_id / org_id` | `vehicleId / orgId` |
| `vms_vehicle.self_code` | `userDefinedCode` → `erpCode` → `vehicleId` |
| `vms_vehicle.plate` | `lisPlate` |
| `vms_vehicle.time / day` | `lastTime`（day 取前 10 字符） |
| `vms_vehicle.location / issue` | `location / alarmText` |
| `vms_vehicle.online / offline_seconds / alarm_flags / speed_kph / lon / lat` | 同名扩展字段 |

### 主键：为什么必须是字符串，不能给库加数字 id

`d_vehicleinfo.vehicle_id` 是 CHAR(32)，注释原文「车辆ID，车牌号+颜色」——
它天然是**非数字**的（`京A12345-1`），且是部标 808 / ERP / 报警联动表的关联键。
曾经有一版客户端 ABI 用 `uint32 ids[]` + `"%06u"` 反向编码，接真实库后这类主键
**根本编码不进 uint32**，接口直接废掉。所以 v5 把 `vms_query.ids` 改成字符串数组，
`biz_param.ids` 与 `data.items[].vehicleId` 同为字符串，端到端不做任何转换。

> **不建议**为了客户端方便给 `d_vehicleinfo` 加自增数字列：
> - 这是存量 VMS3.0 库表，注释里明确要求与 `d_ve_alarm_switch` / `d_ve_over_speed` /
>   `d_ve_area_ovspeed` / `d_ve_area_switch` / `d_vehicle_term` / `d_vehicle_camera`
>   六张表联动，加列 = 六张表一起改 + 数据迁移；
> - CHAR(32) 定长做 B-tree 索引对 3 万行量级完全够用（点查差距在微秒级，
>   真正的瓶颈是 `keyword LIKE '%京%'` 这类全表扫，与主键类型无关）；
> - 外部系统按车牌+颜色关联，凭空引入第二个主键必然带来同步不一致。
>
> 唯一该考虑"不变代理键"的理由是**车牌会变更**（换牌/过户后历史定位断链），
> 那是数据模型层面的独立改造，且新代理键仍是字符串（UUID）。

---

## 6. 错误码与排障

| 现象 | 结果码 | 原因 |
|---|---|---|
| 缺 `biz_type` | 2 BADREQ | 客户端 TLV `0x0201` 未带 |
| `biz_param` 不是合法 JSON | 2 BADREQ | 文本字段内嵌 NUL 会被 bizd 整字段丢弃 |
| Token 空/全零 | 3 UNAUTHORIZED | netd 未透传外层 `0x0006`，或客户端未登录 |
| 未知 `biz_type` | 6 UNSUPPORTED | |
| 响应被截断（客户端 JSON 解析失败） | — | 单页 `limit` 过大，见 §2 单页上限 |

排障工具：

* `python3 shell/biz_probe.py --selftest` —— 绕过 netd/bizd，直接按 wire 帧打 9003，校验 total 与分页连续性。
* `bash shell/e2e_m3.sh` —— 全链路（netd → bizd → biz），跑 stats/companies/vehicles 全部用例。
* `python shell/vmscore_smoke.py --pub <der>` —— Windows 侧 ctypes 直连 `vms_core.dll`，验证 ABI 与解析。
