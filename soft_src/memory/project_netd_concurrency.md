---
name: netd 并发模型约束
description: netd 为何不使用 q_net_srv，而自建 accept 循环 + 有界线程池派发整条连接
type: project
---

`soft_src/netd` **不使用** `q_net_srv`，而是：自建 accept 循环（reactor 监听 fd）→ **有界** `q_tpool` 派发【整条连接】→ worker 线程内跑**阻塞式** `q_chan` 全流程（握手 → 收包 → 状态/配额校验 → 转发 bizd → 加密回写）。

**Why:** `q_chan` 的 `handshake`/`receive` 是**阻塞多轮往返**语义——握手走裸 DER 字节流（2 字节大端长度 + SM2 DER），且必须握手成功后（`established==1`）才允许收发加密帧（见 `q_chan_send/recv/transmit/receive` 首部守卫）。而 `q_net_srv` 会把连接 fd 置为**非阻塞**，且 reactor 已把数据**读空**后才按 chunk 派发给 worker。两者叠加的后果：① 非阻塞 fd 让 `q_net_recv_blocking` 遇 EAGAIN 直接返回 -1，**握手恒失败**；② 握手所需字节已被 reactor 消费，等待**永不满足**。

**How to apply:** 不要把 netd 重构成使用 `q_net_srv`（会静默握手失败）。仅当 q_chan 未来提供非阻塞 / 状态机式接口时，才可重新评估。该约束同时写在 `soft_src/netd/main.c` 头部注释与 `srv_netd.h` 中。
