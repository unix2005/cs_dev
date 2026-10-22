#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
vms_core.dll 冒烟测试（Windows 侧，ctypes 直连 ABI）

用途：绕开 WinUI3 界面，**直接**用 ctypes 调 native 门面，验证
      登录 -> 组织机构(companies) -> 车辆分页(vehicles) 这条链路，
      以及 ABI v4 新增的字符串主键字段（vms_company.org_id / vms_vehicle.vehicle_id+org_id）
      是否真的从下游 JSON 搬到了结构体里。

      这是唯一能在不改 UI 的前提下，确认"C 层解析正确"的低成本手段：
      结构体的字段顺序/宽度写错，这里会立刻以乱码或崩溃的形式暴露。

用法：
    python shell/vmscore_smoke.py --pub E:\\win_e\\cs_dev\\tmp\\vmsconf\\netd_sm2.pub
    python shell/vmscore_smoke.py --host 127.0.0.1 --port 18099 \
        --user qiaoshui --pass 123456 --pub <pub.der> --org-id 14 --limit 5

前置：netd(18099) / bizd / login(9002) / biz(9003) 已在跑（见 shell/e2e_m3.sh）。
"""
import argparse
import ctypes
import os
import sys

# ----------------------------- ABI 常量（必须与 vms_api.h 一致） -----------------------------
VMS_ABI_VERSION = 4

VMS_CODE_MAX = 24
VMS_PLATE_MAX = 16
VMS_TEXT_MAX = 64
VMS_TIME_MAX = 24
VMS_ISSUE_MAX = 40
VMS_KEY_MAX = 40          # v4：字符串主键
VMS_HOST_MAX = 64
VMS_USER_MAX = 64
VMS_PASS_MAX = 64
VMS_OTP_MAX = 16
VMS_DEVID_HEX_MAX = 65
VMS_PATH_MAX = 260
VMS_QUERY_MAX_IDS = 256


class VmsStats(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("total", ctypes.c_uint32),
        ("online", ctypes.c_uint32),
        ("offline", ctypes.c_uint32),
        ("excluded", ctypes.c_uint32),
        ("alarm_unhandled", ctypes.c_uint32),
        ("online_rate", ctypes.c_float),
        ("reserved", ctypes.c_uint32),
    ]


class VmsCompany(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("id", ctypes.c_uint32),
        ("parent_id", ctypes.c_uint32),
        ("online", ctypes.c_uint32),
        ("count", ctypes.c_uint32),
        ("child_count", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("name", ctypes.c_char * VMS_TEXT_MAX),
        ("org_id", ctypes.c_char * VMS_KEY_MAX),      # v4
    ]


class VmsVehicle(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("id", ctypes.c_uint32),
        ("online", ctypes.c_uint32),
        ("offline_seconds", ctypes.c_uint32),
        ("alarm_flags", ctypes.c_uint32),
        ("speed_kph", ctypes.c_float),
        ("lon", ctypes.c_double),
        ("lat", ctypes.c_double),
        ("self_code", ctypes.c_char * VMS_CODE_MAX),
        ("plate", ctypes.c_char * VMS_PLATE_MAX),
        ("time", ctypes.c_char * VMS_TIME_MAX),
        ("location", ctypes.c_char * VMS_TEXT_MAX),
        ("day", ctypes.c_char * VMS_TIME_MAX),
        ("issue", ctypes.c_char * VMS_ISSUE_MAX),
        ("vehicle_id", ctypes.c_char * VMS_KEY_MAX),  # v4
        ("org_id", ctypes.c_char * VMS_KEY_MAX),      # v4
    ]


class VmsQuery(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("company_id", ctypes.c_uint32),
        ("offset", ctypes.c_uint32),
        ("limit", ctypes.c_uint32),
        ("online_only", ctypes.c_uint32),
        ("alarm_only", ctypes.c_uint32),
        ("alarm_mask", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
        ("keyword", ctypes.c_char * VMS_TEXT_MAX),
        ("ids", ctypes.c_uint32 * VMS_QUERY_MAX_IDS),
        ("id_count", ctypes.c_uint32),
    ]


class VmsCredentials(ctypes.Structure):
    _pack_ = 8
    _fields_ = [
        ("host", ctypes.c_char * VMS_HOST_MAX),
        ("port", ctypes.c_uint16),
        ("reserved16", ctypes.c_uint16),
        ("user", ctypes.c_char * VMS_USER_MAX),
        ("password", ctypes.c_char * VMS_PASS_MAX),
        ("otp", ctypes.c_char * VMS_OTP_MAX),
        ("devid_hex", ctypes.c_char * VMS_DEVID_HEX_MAX),
        ("pubkey_path", ctypes.c_char * VMS_PATH_MAX),
        ("knock", ctypes.c_char * VMS_HOST_MAX),
        ("spa_key_path", ctypes.c_char * VMS_PATH_MAX),
        ("pbkdf2_iter", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32),
    ]


def s(buf):
    """fixed char[] -> str（UTF-8，遇到 NUL 截断）"""
    raw = bytes(buf)
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_dll = os.path.normpath(
        os.path.join(here, "..", "soft_src", "win_vms_v1", "native", "build", "vms_core.dll"))

    ap = argparse.ArgumentParser(description="vms_core.dll 冒烟测试")
    ap.add_argument("--dll", default=default_dll)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=18099)
    ap.add_argument("--user", default="qiaoshui")
    ap.add_argument("--pass", dest="password", default="123456")
    ap.add_argument("--pub", required=True, help="服务端 SM2 公钥 DER 路径")
    ap.add_argument("--devid-hex", default="")
    ap.add_argument("--org-id", type=int, default=0, help="按组织过滤（0=全部）")
    ap.add_argument("--keyword", default="")
    ap.add_argument("--limit", type=int, default=5)
    ap.add_argument("--offset", type=int, default=0)
    args = ap.parse_args()

    dll = ctypes.CDLL(args.dll)
    dll.vms_abi_version.restype = ctypes.c_uint32
    dll.vms_struct_size.restype = ctypes.c_size_t
    dll.vms_struct_size.argtypes = [ctypes.c_int32]
    dll.vms_create.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    dll.vms_login.argtypes = [ctypes.c_void_p, ctypes.POINTER(VmsCredentials)]
    dll.vms_get_companies.argtypes = [ctypes.c_void_p, ctypes.POINTER(VmsCompany),
                                      ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    dll.vms_query_vehicles.argtypes = [ctypes.c_void_p, ctypes.POINTER(VmsQuery),
                                       ctypes.POINTER(VmsVehicle),
                                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    dll.vms_last_error.restype = ctypes.c_char_p

    # ---------- ABI 自检 ----------
    abi = dll.vms_abi_version()
    print(f"ABI 版本: DLL={abi} 期望={VMS_ABI_VERSION}")
    if abi != VMS_ABI_VERSION:
        print("!! ABI 不匹配，脚本的 ctypes 结构体可能已过期", file=sys.stderr)
    for kind, cls, name in ((3, VmsCompany, "vms_company"),
                            (4, VmsVehicle, "vms_vehicle"),
                            (5, VmsQuery, "vms_query")):
        native = dll.vms_struct_size(kind)
        managed = ctypes.sizeof(cls)
        flag = "OK" if native == managed else "MISMATCH"
        print(f"  [{flag}] {name}: native={native}B managed={managed}B")

    # ---------- create ----------
    ctx = ctypes.c_void_p()
    rc = dll.vms_create(None, ctypes.byref(ctx))
    if rc != 0:
        print("vms_create 失败:", rc)
        return 1
    print("vms_create OK")

    # ---------- login ----------
    cred = VmsCredentials()
    cred.host = args.host.encode()
    cred.port = args.port
    cred.user = args.user.encode()
    cred.password = args.password.encode()
    cred.otp = b"000000"
    if args.devid_hex:
        cred.devid_hex = args.devid_hex.encode()
    cred.pubkey_path = os.path.abspath(args.pub).encode()

    rc = dll.vms_login(ctx, ctypes.byref(cred))
    cred.password = b"\0" * VMS_PASS_MAX          # 口令用完即清
    if rc != 0:
        err = dll.vms_last_error(ctx)
        print(f"vms_login 失败 rc={rc} {err.decode('utf-8', 'replace') if err else ''}")
        return 1
    print("vms_login OK（已拿到 Token）")

    try:
        # ---------- companies ----------
        total = ctypes.c_size_t(0)
        rc = dll.vms_get_companies(ctx, None, 0, ctypes.byref(total))
        print(f"vms_get_companies(探测) rc={rc} 组织总数={total.value}")
        if total.value:
            buf = (VmsCompany * total.value)()
            got = ctypes.c_size_t(0)
            rc = dll.vms_get_companies(ctx, buf, total.value, ctypes.byref(got))
            if rc != 0:
                print("vms_get_companies 失败 rc=", rc)
                return 1
            print(f"  取回 {got.value} 个组织（前 5 个）：")
            for i in range(min(5, got.value)):
                c = buf[i]
                print(f"    id={c.id:<5} parent={c.parent_id:<5} orgId={s(c.org_id):<6} "
                      f"name={s(c.name):<14} 车辆 {c.online}/{c.count} 下级={c.child_count}")

        # ---------- vehicles ----------
        q = VmsQuery()
        q.company_id = args.org_id
        q.offset = args.offset
        q.limit = args.limit
        q.alarm_mask = 0xFFFF
        if args.keyword:
            q.keyword = args.keyword.encode()

        rows = (VmsVehicle * (args.limit or 50))()
        matched = ctypes.c_size_t(0)
        rc = dll.vms_query_vehicles(ctx, ctypes.byref(q), rows, len(rows), ctypes.byref(matched))
        # rc == -4 (BUFFER_SMALL) 表示后面还有，属正常
        if rc not in (0, -4):
            err = dll.vms_last_error(ctx)
            print(f"vms_query_vehicles 失败 rc={rc} {err.decode('utf-8', 'replace') if err else ''}")
            return 1
        print(f"vms_query_vehicles rc={rc} 命中总数={matched.value} 本页 {min(len(rows), max(0, matched.value - args.offset))} 条：")
        n = min(len(rows), max(0, matched.value - args.offset))
        for i in range(n):
            v = rows[i]
            print(f"    id={v.id:<7} vehicleId={s(v.vehicle_id):<8} orgId={s(v.org_id):<6} "
                  f"plate={s(v.plate):<10} 自编号={s(v.self_code):<8} "
                  f"online={v.online} speed={v.speed_kph:.0f} "
                  f"({v.lon:.5f},{v.lat:.5f}) {s(v.location)}")
        return 0
    finally:
        dll.vms_logout(ctx)
        dll.vms_destroy(ctx)
        print("已登出并释放")


if __name__ == "__main__":
    sys.exit(main())
