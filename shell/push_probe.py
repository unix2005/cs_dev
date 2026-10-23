#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
push 推送微服务裸协议探针（q_service_push / pushd，默认 sock=/var/run/gtkvms/pushd.sock）

用途：绕过 netd_B / knockd_B / 加密通道，**直接**按 wire 契约与下游推送微服务对话，
      快速验证「推送会话建立 / 增量订阅 / TRAJ-ALARM 下发 / 组织子树过滤」是否正确。

      wire 契约（与 srv_push.h 文件头、netd_B 的 netd_ipc.c 完全一致，勿漂移）：
      - 传输：Unix Socket；分帧 [4 字节大端长度][TLV 序列]。
      - C→S BEGIN     : TLV(CMD=0x0050) + TLV(SRCIP) + TLV(CHANID) + TLV(USERID)
                         + TLV(TOKEN 32B)? + TLV(USERNAME)?   应答：TLV(RESULT=uint32 BE)
      - C→S SUB_DELTA : TLV(CMD=0x0051) + TLV(CHANID) + TLV(BODY=OP 1B + VID*n)  免回执
      - S→C 推送      : TLV(CMD=0x0052 TRAJ | 0x0053 ALARM) + TLV(BODY=推送域 TLV)

用法（手动模式）：
    # 起服务（默认校验 token）：q_service_push -s /tmp/pushd.sock
    python3 push_probe.py --sock /tmp/pushd.sock --user qiaoshui \
        --add 京A10001-1,京A10003-2 --listen 6
    # 减订：再发一帧 REMOVE（本地集合为真相，服务端合并）
    python3 push_probe.py --sock /tmp/pushd.sock --user qiaoshui --remove 京A10003-2
    # 不校验 token 的服务（q_service_push --no-token）可用 --no-token 省掉 TOKEN TLV
    python3 push_probe.py --sock /tmp/pushd.sock --no-token --add 京A10001-1 --listen 3

自检模式（--selftest，服务须以默认参数运行）：
    python3 push_probe.py --sock /tmp/pushd.sock --selftest
  场景：① 无 token BEGIN 应被拒（RESULT=3）；② qiaoshui（org 1）订阅两车，
        TRAJ 按订阅精确下发、ALARM 全放行（根组织）；③ tayun（org 2）同订两车，
        TRAJ 同样全发（轨迹不按组织过滤），但 org 31 的报警必须被子树过滤掉；
  ④ REMOVE 后该车轨迹停发。断言全部落在 wire 层，与 C 侧 selftest 互补。
"""
import argparse
import os
import selectors
import socket
import struct
import sys
import time

# ===================== wire 常量（include_ipc_proto.h / q_chan.h，勿在此重复定义业务语义） =====================
Q_IPC_T_CMD      = 0x0001
Q_IPC_T_BODY     = 0x0002
Q_IPC_T_SRCIP    = 0x0003
Q_IPC_T_CHANID   = 0x0004
Q_IPC_T_USERID   = 0x0005
Q_IPC_T_TOKEN    = 0x0006
Q_IPC_T_RESULT   = 0x0007
Q_IPC_T_USERNAME = 0x0101

Q_IPC_T_PUSH_OP     = 0x0301
Q_IPC_T_PUSH_VID    = 0x0302
Q_IPC_T_PUSH_LON    = 0x0303
Q_IPC_T_PUSH_LAT    = 0x0304
Q_IPC_T_PUSH_SPEED  = 0x0305
Q_IPC_T_PUSH_TV     = 0x0306
Q_IPC_T_PUSH_ADDR   = 0x0307
Q_IPC_T_PUSH_ONLINE = 0x0308
Q_IPC_T_PUSH_AFLAGS = 0x0309
Q_IPC_T_PUSH_ATYPE  = 0x030A
Q_IPC_T_PUSH_ATEXT  = 0x030B
Q_IPC_T_PUSH_ORG    = 0x030C
Q_IPC_T_PUSH_PLATE  = 0x030D

Q_PUSH_OP_ADD, Q_PUSH_OP_REMOVE = 1, 2
Q_PUSH_ALARM_NAME = {
    0x0001: "OVERSPEED", 0x0002: "FATIGUE", 0x0004: "OFFLINE",
    0x0008: "GEOFENCE", 0x0010: "GNSS_LOST",
}

Q_CHAN_CMD_PUSH_BEGIN     = 0x0050
Q_CHAN_CMD_PUSH_SUB_DELTA = 0x0051
Q_CHAN_CMD_PUSH_TRAJ      = 0x0052
Q_CHAN_CMD_PUSH_ALARM     = 0x0053

Q_IPC_R_OK, Q_IPC_R_UNAUTHORIZED = 0, 3


# ===================== TLV / 帧 编解码 =====================
def tlv(tag, val):
    return struct.pack(">HH", tag, len(val)) + val


def tlv_u16(tag, v):
    return tlv(tag, struct.pack(">H", v))


def tlv_u32(tag, v):
    return tlv(tag, struct.pack(">I", v))


def tlv_text(tag, s):
    return tlv(tag, s.encode("utf-8"))


def frame(body):
    return struct.pack(">I", len(body)) + body


def tlv_iter(buf):
    """遍历 TLV 序列，yield (tag, val)。遇非法结构停止（与服务端同策略：粗鲁但够用）。"""
    off = 0
    while off + 4 <= len(buf):
        tag, vlen = struct.unpack_from(">HH", buf, off)
        off += 4
        if off + vlen > len(buf):
            break
        yield tag, buf[off:off + vlen]
        off += vlen


def f64_be(b):
    return struct.unpack(">d", b)[0] if len(b) == 8 else None


def u32_be(b):
    return struct.unpack(">I", b)[0] if len(b) == 4 else None


def text(b):
    return b.decode("utf-8", "replace")


# ===================== 探针主体 =====================
class PushProbe(object):
    def __init__(self, sock_path, user, token, timeout=10.0):
        self.sock_path = sock_path
        self.user = user
        self.token = token            # bytes(32) 或 None（--no-token）
        self.timeout = timeout
        self.s = None

    def connect(self):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(self.timeout)
        self.s.connect(self.sock_path)
        return self

    def close(self):
        if self.s:
            self.s.close()
            self.s = None

    def _send(self, body):
        self.s.sendall(frame(body))

    def _recv_frame(self):
        """收一帧 [4B BE len][TLV...]；对端关闭/超时返回 None。"""
        hdr = b""
        while len(hdr) < 4:
            chunk = self.s.recv(4 - len(hdr))
            if not chunk:
                return None
            hdr += chunk
        n = struct.unpack(">I", hdr)[0]
        if n == 0 or n > 4 * 1024 * 1024:
            raise RuntimeError("非法长度前缀: %d" % n)
        buf = b""
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    # ---------- C→S ----------
    def begin(self):
        """发 BEGIN，读 RESULT 应答。返回结果码（None = 没收到应答）。"""
        body = tlv_u16(Q_IPC_T_CMD, Q_CHAN_CMD_PUSH_BEGIN)
        # SRCIP: [1B 族=4][16B 地址]（IPv4 占前 4B，其余补零）；pushd 忽略，netd_B 对账用
        body += tlv(Q_IPC_T_SRCIP, b"\x04" + b"\x00" * 15)
        body += tlv(Q_IPC_T_CHANID, bytes(range(32)))       # 探针假 channel_id（32B）
        body += tlv(Q_IPC_T_USERID, bytes(range(32)))       # 探针假 user_id（SM3(user) 形状）
        if self.token is not None:
            body += tlv(Q_IPC_T_TOKEN, self.token)
        if self.user:
            body += tlv_text(Q_IPC_T_USERNAME, self.user)
        self._send(body)

        resp = self._recv_frame()
        if resp is None:
            return None
        for tag, val in tlv_iter(resp):
            if tag == Q_IPC_T_RESULT and len(val) == 4:
                return u32_be(val)
        return None

    def sub_delta(self, op, vids):
        """发 SUB_DELTA（免回执）。op: 1=ADD / 2=REMOVE；vids: 字符串列表。"""
        inner = tlv(Q_IPC_T_PUSH_OP, bytes([op]))   # OP 是 TLV（tag 0x0301，val 1B），不是裸字节
        for vid in vids:
            inner += tlv_text(Q_IPC_T_PUSH_VID, vid)
        body = tlv_u16(Q_IPC_T_CMD, Q_CHAN_CMD_PUSH_SUB_DELTA)
        body += tlv(Q_IPC_T_CHANID, bytes(range(32)))
        body += tlv(Q_IPC_T_BODY, inner)
        self._send(body)

    # ---------- S→C ----------
    def recv_push(self):
        """收一帧推送；返回 (cmd, fields dict) 或 None（超时/关闭）。"""
        buf = self._recv_frame()
        if buf is None:
            return None
        cmd, fields = None, {}
        for tag, val in tlv_iter(buf):
            if tag == Q_IPC_T_CMD and len(val) >= 2:
                cmd = struct.unpack(">H", val[:2])[0]
            elif tag == Q_IPC_T_BODY:
                for t2, v2 in tlv_iter(val):
                    fields[t2] = v2
        return cmd, fields


def fmt_traj(f):
    lon, lat = f64_be(f.get(Q_IPC_T_PUSH_LON, b"")), f64_be(f.get(Q_IPC_T_PUSH_LAT, b""))
    return ("TRAJ %s lon=%.5f lat=%.5f speed=%s online=%s aflags=0x%02x tv=%s addr=%s"
            % (text(f.get(Q_IPC_T_PUSH_VID, b"")), lon or 0.0, lat or 0.0,
               u32_be(f.get(Q_IPC_T_PUSH_SPEED, b"")) or 0,
               u32_be(f.get(Q_IPC_T_PUSH_ONLINE, b"")) or 0,
               u32_be(f.get(Q_IPC_T_PUSH_AFLAGS, b"")) or 0,
               text(f.get(Q_IPC_T_PUSH_TV, b"")), text(f.get(Q_IPC_T_PUSH_ADDR, b""))))


def fmt_alarm(f):
    at = u32_be(f.get(Q_IPC_T_PUSH_ATYPE, b"")) or 0
    lon, lat = f64_be(f.get(Q_IPC_T_PUSH_LON, b"")), f64_be(f.get(Q_IPC_T_PUSH_LAT, b""))
    return ("ALARM %s type=0x%04x(%s) org=%s plate=%s lon=%.5f lat=%.5f text=%s addr=%s"
            % (text(f.get(Q_IPC_T_PUSH_VID, b"")), at,
               Q_PUSH_ALARM_NAME.get(at, "?"), text(f.get(Q_IPC_T_PUSH_ORG, b"")),
               text(f.get(Q_IPC_T_PUSH_PLATE, b"")), lon or 0.0, lat or 0.0,
               text(f.get(Q_IPC_T_PUSH_ATEXT, b"")), text(f.get(Q_IPC_T_PUSH_ADDR, b""))))


def make_token(arg):
    if arg == "rand":
        return os.urandom(32)
    raw = bytes.fromhex(arg)
    if len(raw) != 32:
        raise SystemExit("token 须为 32B（hex 64 字符）或 'rand'，当前 %dB" % len(raw))
    return raw


# ===================== 自检 =====================
def selftest(sock_path):
    fails = []

    def check(cond, what):
        print("  [%s] %s" % ("ok" if cond else "FAIL", what))
        if not cond:
            fails.append(what)

    print("== push probe selftest（服务须以默认参数运行：校验 token、6 车、报警开） ==")

    # ---- ① 无 token BEGIN 应被拒（RESULT=3 UNAUTHORIZED）----
    p = PushProbe(sock_path, "qiaoshui", None).connect()
    r = p.begin()
    check(r == Q_IPC_R_UNAUTHORIZED, "无 token BEGIN 被拒（RESULT=3）")
    p.close()

    # ---- ② qiaoshui（org 1 根组织）：订阅两车，报警全放行 ----
    pa = PushProbe(sock_path, "qiaoshui", os.urandom(32)).connect()
    r = pa.begin()
    check(r == Q_IPC_R_OK, "qiaoshui BEGIN 成功（RESULT=0）")
    pa.sub_delta(Q_PUSH_OP_ADD, ["京A10001-1", "京A10003-2"])

    # ---- ③ tayun（org 2 塔运司）：同订两车；org 31 的报警应被子树过滤 ----
    pb = PushProbe(sock_path, "tayun", os.urandom(32)).connect()
    r = pb.begin()
    check(r == Q_IPC_R_OK, "tayun BEGIN 成功（RESULT=0）")
    pb.sub_delta(Q_PUSH_OP_ADD, ["京A10001-1", "京A10003-2"])

    # ---- 并发监听：桩报警周期 12s/17s/23s，首触分别在服务启动后 3s/8s/13s，
    #      收听 16s 至少覆盖每种报警的第一次 ----
    # 读取模型：selectors 同时监听两个 socket —— 曾用"A 阻塞读一帧再读 B"的
    # 交替模型，结果读取速率追不上发送速率（每侧 2 帧/s），报警在缓冲区里
    # 积压数秒才被读到，断言全乱。一帧到就一帧读，速率不再是瓶颈。
    sel = selectors.DefaultSelector()
    conns = []   # (tag, probe, traj_set, alarm_set)
    for tag, probe in (("A", pa), ("B", pb)):
        conns.append((tag, probe, set(), set()))
        sel.register(probe.s, selectors.EVENT_READ, len(conns) - 1)
    traj_a, alarm_a = conns[0][2], conns[0][3]
    traj_b, alarm_b = conns[1][2], conns[1][3]

    deadline = time.time() + 16.0
    removed_sent = False
    n_traj_after_rm = {"京A10003-2": 0}

    def read_one(idx):
        tag, probe, traj_set, alarm_set = conns[idx]
        try:
            got = probe.recv_push()
        except socket.timeout:
            return
        if got is None:
            return
        cmd, f = got
        vid = text(f.get(Q_IPC_T_PUSH_VID, b""))
        if cmd == Q_CHAN_CMD_PUSH_TRAJ:
            print("    [%s] %s" % (tag, fmt_traj(f)))
            traj_set.add(vid)
            # 只统计 A（conns[0]）的帧 —— B 从没删过订阅，它的轨迹不能算进 A 的账
            if idx == 0 and removed_sent and vid in n_traj_after_rm:
                n_traj_after_rm[vid] += 1
        elif cmd == Q_CHAN_CMD_PUSH_ALARM:
            print("    [%s] %s" % (tag, fmt_alarm(f)))
            alarm_set.add((vid, u32_be(f.get(Q_IPC_T_PUSH_ATYPE, b"")) or 0))

    def drain(quiet_sec):
        """把两个 socket 的在途帧读干净（静默 quiet_sec 即认为排空），帧丢弃。"""
        until = time.time() + quiet_sec
        while time.time() < until:
            for key, _ in sel.select(timeout=0.05):
                read_one_discard(key.data)

    def read_one_discard(idx):
        tag, probe, _, _ = conns[idx]
        try:
            probe.recv_push()
        except socket.timeout:
            pass

    while time.time() < deadline:
        remain = deadline - time.time()
        for key, _ in sel.select(timeout=max(0.05, remain)):
            read_one(key.data)

        # 收听中段对 A 发 REMOVE，验证该车轨迹停发。
        # 注意口径：REMOVE 生效前发出的在途帧可能还在 A 的缓冲区里，
        # 先 drain 排空再开始严格计数，否则旧帧会被误计成"REMOVE 后仍收到"。
        if not removed_sent and time.time() > deadline - 8.0:
            pa.sub_delta(Q_PUSH_OP_REMOVE, ["京A10003-2"])
            removed_sent = True
            print("    -- 已向 A(qiaoshui) 发送 REMOVE 京A10003-2，排空在途帧后严格计数 --")
            drain(0.8)

    check(traj_a >= {"京A10001-1"}, "A(qiaoshui) 收到 京A10001-1 轨迹")
    check(traj_b >= {"京A10001-1", "京A10003-2"}, "B(tayun) 两车轨迹都收到（轨迹不按组织过滤）")
    check(n_traj_after_rm["京A10003-2"] == 0, "REMOVE 生效后 A 不再收到 京A10003-2 轨迹")
    check(("京A10001-1", 0x0001) in alarm_a, "A 收到 京A10001-1 超速报警（org 21）")
    check(("京A10003-2", 0x0002) in alarm_a, "A 收到 京A10003-2 疲劳报警（org 31，根组织全放行）")
    check(("京A10001-1", 0x0001) in alarm_b, "B 收到 京A10001-1 超速报警（org 21 在 2 子树内）")
    check(("京A10003-2", 0x0002) not in alarm_b, "B 收不到 京A10003-2 报警（org 31 不在 2 子树内，被过滤）")

    pa.close()
    pb.close()
    print("== push probe selftest: %s ==" % ("FAIL(%d)" % len(fails) if fails else "ALL PASS"))
    return 1 if fails else 0


# ===================== 手动模式 =====================
def main():
    ap = argparse.ArgumentParser(description="pushd 裸协议探针（UDS wire 契约直连）")
    ap.add_argument("--sock", default="/var/run/gtkvms/pushd.sock", help="pushd UDS 路径")
    ap.add_argument("--user", default="qiaoshui", help="USERNAME（决定报警的组织子树过滤根）")
    ap.add_argument("--token", default="rand", help="32B hex（64 字符）或 rand；--no-token 时忽略")
    ap.add_argument("--no-token", action="store_true", help="不发 TOKEN TLV（测服务端拒绝路径）")
    ap.add_argument("--add", default="", help="ADD 订阅，逗号分隔车辆主键")
    ap.add_argument("--remove", default="", help="REMOVE 订阅，逗号分隔车辆主键")
    ap.add_argument("--listen", type=float, default=5.0, help="收听推送秒数（0=不等）")
    ap.add_argument("--timeout", type=float, default=10.0, help="单次 recv 超时秒")
    ap.add_argument("--selftest", action="store_true", help="运行内置场景自检（约 20s）")
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.sock)

    token = None if args.no_token else make_token(args.token)
    p = PushProbe(args.sock, args.user, token, timeout=args.timeout).connect()
    try:
        r = p.begin()
        if r is None:
            print("BEGIN 无应答（连接被关闭？）")
            return 1
        print("BEGIN -> RESULT=%u (%s)" % (r, "OK" if r == Q_IPC_R_OK else "被拒"))
        if r != Q_IPC_R_OK:
            return 2
        if args.add:
            p.sub_delta(Q_PUSH_OP_ADD, [x for x in args.add.split(",") if x])
        if args.remove:
            p.sub_delta(Q_PUSH_OP_REMOVE, [x for x in args.remove.split(",") if x])
        end = time.time() + args.listen
        while time.time() < end:
            try:
                got = p.recv_push()
            except socket.timeout:
                continue
            if got is None:
                print("连接关闭")
                break
            cmd, f = got
            if cmd == Q_CHAN_CMD_PUSH_TRAJ:
                print(fmt_traj(f))
            elif cmd == Q_CHAN_CMD_PUSH_ALARM:
                print(fmt_alarm(f))
        return 0
    finally:
        p.close()


if __name__ == "__main__":
    sys.exit(main())
