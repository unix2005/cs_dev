#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
biz 微服务裸协议探针（q_service_biz，默认 9003）

用途：绕过 netd / bizd / 加密通道，**直接**按 wire 契约 [4 字节大端长度][JSON]
      与下游业务微服务对话，用于快速验证 biz_type 的 data 域形状是否正确。

      wire 契约与 bizd 完全一致（见 include/include_ipc_proto.h「bizd -> 微服务 JSON 包格式」），
      所以本脚本打出来的响应，就是客户端最终会拿到的下游 JSON。

用法：
    python3 biz_probe.py --type stats
    python3 biz_probe.py --type companies --limit 5
    python3 biz_probe.py --type companies --param '{"parent_org_id":"14","recursive":1}'
    python3 biz_probe.py --type vehicles --param '{"org_id":"14","recursive":1}' --limit 3
    python3 biz_probe.py --type vehicles --param '{"keyword":"京A"}' --offset 0 --limit 2

注意：服务端默认校验 token（非空且非全零），裸测时要么给 --token，
      要么用 --no-token 启动服务（q_service_biz --no-token）。
"""
import argparse
import json
import socket
import struct
import sys


def call(host, port, req, timeout=10.0):
    """发一帧 JSON，收一帧 JSON。"""
    s = socket.create_connection((host, port), timeout=timeout)
    try:
        body = json.dumps(req, ensure_ascii=False).encode("utf-8")
        s.sendall(struct.pack(">I", len(body)) + body)

        hdr = recv_exact(s, 4)
        if not hdr:
            raise RuntimeError("连接被对端关闭（未收到长度前缀）")
        n = struct.unpack(">I", hdr)[0]
        if n == 0 or n > 4 * 1024 * 1024:
            raise RuntimeError("非法长度前缀: %d" % n)
        buf = recv_exact(s, n)
        if buf is None:
            raise RuntimeError("响应体不完整")
        return json.loads(buf.decode("utf-8"))
    finally:
        s.close()


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


# ===================== 内置自检（--selftest） =====================
# 期望值来自桩数据的构造规则：车辆只挂在叶子组织上，父组织计数 = 各叶子之和。
#   org 14 子树 = 1401(210) + 1402(188) = 398
#   org 11 子树 = 1101(250) + 1102(220) = 470
#   recursive=0 时父组织直属车辆为 0（车辆都挂在叶子上）
CASES = [
    ("companies", "",                                    25),
    ("companies", '{"parent_org_id":"1","recursive":0}',  18),
    ("companies", '{"parent_org_id":"14","recursive":1}',  3),
    ("companies", '{"parent_org_id":"11","recursive":1}',  3),
    ("vehicles",  "",                                  34554),
    ("vehicles",  '{"org_id":"14","recursive":1}',        398),
    ("vehicles",  '{"org_id":"11","recursive":1}',        470),
    ("vehicles",  '{"org_id":"14","recursive":0}',          0),
    ("vehicles",  '{"online_only":1}',                 10846),
    ("vehicles",  '{"ids":["000001","000002"]}',            2),
]


def selftest(host, port, token):
    """跑内置用例：校验 total 与分页不重不漏。"""
    fail = 0
    print("== biz 微服务自检 (%s:%d) ==" % (host, port))

    for btype, param, want_total in CASES:
        rsp = call(host, port, build_req(btype, param, 0, 2, token))
        data = rsp.get("data") or {}
        got = data.get("total")
        ok = (rsp.get("result") == 0 and got == want_total)
        print("  [%s] %-46s total=%-8s 期望=%-8d %s"
              % ("OK" if ok else "NG", "%s %s" % (btype, param or "{}"),
                 got, want_total, "" if ok else "<<< MISMATCH"))
        if not ok:
            fail += 1

    # 分页不重不漏：第 0 页与第 1 页的主键集合必须不相交，且第 1 页第 1 条 = 全局第 limit 条
    p0 = (call(host, port, build_req("vehicles", "", 0, 5, token)).get("data") or {})
    p1 = (call(host, port, build_req("vehicles", "", 5, 5, token)).get("data") or {})
    ids0 = [it.get("vehicleId") for it in p0.get("items", [])]
    ids1 = [it.get("vehicleId") for it in p1.get("items", [])]
    overlap = set(ids0) & set(ids1)
    ok = (not overlap) and len(ids0) == 5 and len(ids1) == 5 and ids0[-1] < ids1[0]
    print("  [%s] 分页连续性: page0=%s page1=%s 重叠=%s"
          % ("OK" if ok else "NG", ids0, ids1, sorted(overlap) if overlap else "无"))
    if not ok:
        fail += 1

    print("== %s ==" % ("全部通过" if fail == 0 else "%d 项失败" % fail))
    return 0 if fail == 0 else 2


def build_req(btype, param, offset, limit, token, req_id="probe-1"):
    params = {"token": token, "biz_type": btype}
    if param:
        params["biz_param"] = param
    if offset:
        params["offset"] = offset
    if limit:
        params["limit"] = limit
    return {
        "ver": 1,
        "id": req_id,
        "cmd": 32,                       # Q_CHAN_CMD_BIZ_QUERY
        "cmd_name": "BIZ_QUERY",
        "ctx": {"src_ip": "127.0.0.1", "user_id": "", "device_id": "", "channel_id": ""},
        "token": token,
        "params": params,
    }


def main():
    ap = argparse.ArgumentParser(description="biz 微服务裸协议探针")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9003)
    ap.add_argument("--type", default="stats",
                    help="params.biz_type: stats / companies / vehicles")
    ap.add_argument("--param", default="",
                    help="params.biz_param 的 JSON 文本（如 '{\"org_id\":\"14\"}'）")
    ap.add_argument("--offset", type=int, default=0)
    ap.add_argument("--limit", type=int, default=0, help="0 = 由服务端取默认页大小")
    ap.add_argument("--token", default="11" * 32, help="64 hex 会话 Token（桩校验只要求非全零）")
    ap.add_argument("--req-id", default="probe-1")
    ap.add_argument("--raw", action="store_true", help="只打印原始响应 JSON（不格式化）")
    ap.add_argument("--selftest", action="store_true",
                    help="跑内置用例（total 期望值 + 分页连续性），用于回归")
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.host, args.port, args.token)

    req = build_req(args.type, args.param, args.offset, args.limit, args.token, args.req_id)
    rsp = call(args.host, args.port, req)

    if args.raw:
        print(json.dumps(rsp, ensure_ascii=False))
        return 0

    result = rsp.get("result")
    print("result=%s  message=%s" % (result, rsp.get("message", "")))
    data = rsp.get("data") or {}
    if "items" in data:
        items = data["items"]
        print("total=%s offset=%s limit=%s 返回 %d 条"
              % (data.get("total"), data.get("offset"), data.get("limit"), len(items)))
        for i, it in enumerate(items):
            print("  ---- [%d] ----" % i)
            print(json.dumps(it, ensure_ascii=False, indent=2, sort_keys=False))
    else:
        print(json.dumps(data, ensure_ascii=False, indent=2))

    return 0 if result == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
