# xrtc-server 已修复 Bug 在 xrtc-server2.0 中的存在性对比报告

> 对比时间：2026-08-07
> 对比对象：`/home/ydqun/workspace/lession/xrtc-server`（Google 风格）→ `/home/ydqun/workspace/lession/xrtc-server2.0`（当前工程，camelCase 风格）
> 前提：两工程为同一份代码，仅代码风格不同。`xrtc-server` 后续修复了一部分 bug。

## 结论速览

- **功能性问题（建议修复）：6 个**
- **无害（死代码）：1 个**
- **已修复 / 原本不存在：6 个**

---

## 一、功能性问题 — 仍存在（6 个）

### 1. UDP socket fd 泄漏

| 项 | 内容 |
|---|---|
| 修复来源 | `xrtc-server` commit `837233a` |
| 位置 | `src/ice/udp_port.cpp` — `~UDPPort()` |
| 问题 | `UDPPort` 构造函数中 `socket_ = CreateUdpSocket(...)`（第 58 行）创建 UDP fd，但析构函数为空，**从不 `close`**。`AsyncUdpSocket` 析构也只删除 IO watcher，不关 fd。每次推拉流结束泄漏一个 fd，长时间运行后端口耗尽、推拉流失败。 |
| 修复方式（xrtc-server） | 析构函数中：`if (_socket != -1) { close(_socket); _socket = -1; }` |

### 2. STOP_PUSH / STOP_PULL / ANSWER 无 TCP 响应

| 项 | 内容 |
|---|---|
| 修复来源 | `xrtc-server` commit `837233a` |
| 位置 | 三处：`src/server/signaling_worker.cpp`、`src/server/rtc_worker.cpp` |
| 问题 | 客户端（或上游 signaling）发 STOP/ANSWER 请求后收不到任何 TCP 响应，只能靠超时。共 3 个缺失点：<br>① `ProcessStopPush`/`ProcessStopPull`/`ProcessAnswer`（signaling_worker.cpp）构造 `RtcMsg` 时**未设置** `msg->worker` / `msg->conn` / `msg->fd`（参数直接写成 `TcpConnection* /*c*/` 忽略）<br>② `RtcWorker::ProcessAnswer`(rtc_worker.cpp:177) / `ProcessStopPush`(188) / `ProcessStopPull`(198) 处理完只打日志，**不回传结果**给 signaling worker<br>③ `SignalingWorker::ProcessRtcMsg`（signaling_worker.cpp:190）的 switch 只有 `CMDNO_PUSH`/`CMDNO_PULL` 走 `ResponseServerOffer`，STOP/ANSWER 落到 `default` 打 "unknown cmdno" 日志 |
| 修复方式（xrtc-server） | ① 三个 `_process_xxx` 补 `msg->worker = this; msg->conn = c; msg->fd = c->fd;`<br>② 三个 `RtcWorker::_process_xxx` 处理完 `if (ret != 0) msg->err_no = -1;` 并 `worker->send_rtc_msg(msg)` 回传<br>③ `_process_rtc_msg` 的 switch 增加 `CMDNO_STOPPUSH`/`CMDNO_STOPPULL`/`CMDNO_ANSWER` 分支 |

### 3. ICE 连接 TIMEOUT 后成僵尸连接

| 项 | 内容 |
|---|---|
| 修复来源 | `xrtc-server` commit `ce31337` |
| 位置 | `src/ice/ice_connection.cpp` — `UpdateState()`（第 238-242 行） |
| 问题 | 超时分支只执行 `set_write_state(STATE_WRITE_TIMEOUT)`，**未调用 `fail_and_destroy()`**。超时连接对象不释放，一直挂在 IceController 的 `_connections` / `_pinged` / `_unpinged` 三集合中，占用 ping 轮询槽位，形成僵尸连接。 |
| 修复方式（xrtc-server） | `set_write_state(STATE_WRITE_TIMEOUT)` 之后追加 `fail_and_destroy()`，通过 `signal_connection_destroy → on_connection_destroyed` 从三集合清理并 `delete this`。 |

### 4. `last_ping_received_` / `last_data_received_` 未更新导致 receiving 误判

| 项 | 内容 |
|---|---|
| 修复来源 | `xrtc-server` commit `ca590d7` |
| 位置 | `src/ice/ice_connection.cpp` — `OnReadPacket()`；`ice_connection.h:129,131` |
| 问题 | `last_ping_received_`（h:129）和 `last_data_received_`（h:131）**声明后从未被赋值**（全文件只有读取、无写入，唯一被赋值的是 `last_ping_response_received_`，见 cpp:158）。`last_received()` 退化为仅依赖 ping 响应时间戳。stable 连接 ping 间隔 2500ms 期间，客户端持续发 RTP/STUN 但 SFU 不记录时间戳，receiving 会被错误翻为 false。 |
| 修复方式（xrtc-server） | `on_read_packet()` 中：非 STUN 数据分支（RTP/DTLS 等）记录 `_last_data_received = rtc::TimeMillis()`；STUN_BINDING_REQUEST 分支记录 `_last_ping_received = rtc::TimeMillis()`。 |

### 5. create_connection 替换旧连接时泄漏

| 项 | 内容 |
|---|---|
| 修复来源 | `xrtc-server` commit `5dbdd12` |
| 位置 | `src/ice/udp_port.cpp`（第 109-113 行） |
| 问题 | 同 address 的新连接覆盖旧连接时，直接 `ret.first->second = conn`。旧连接仍在 IceController 三集合中，但 UDPPort 的 map 已指向新连接，旧连接变僵尸永久泄漏。 |
| 修复方式（xrtc-server） | 覆盖前先取旧连接并 `old_conn->destroy()`（触发 `signal_connection_destroy → IceController 清理 → delete this`），再替换 map 指针。 |

### 6. k_failed 时不删除 30s ICE 超时定时器

| 项 | 内容 |
|---|---|
| 修复来源 | `xrtc-server` commit `4a042b1` |
| 位置 | `src/stream/rtc_stream.cpp` — `OnConnectionState()`（第 34-43 行） |
| 问题 | `OnConnectionState` 只在 `state_ == PeerConnectionState::kConnected` 时删除 `ice_timeout_watcher_`。连接状态到 `kFailed` 时**不删**，已决定销毁的流还要等 30s 定时器二次触发 `OnStreamException`（虽因 map erase 防护不会 double-free，但逻辑不干净，且可能触发非预期回调）。 |
| 修复方式（xrtc-server） | 条件改为 `kConnected || kFailed`。 |

---

## 二、无害 — 死代码（1 个）

### 7. `GetConnectionPingInterval` 中 `weak()` 死代码

| 项 | 内容 |
|---|---|
| 修复来源 | `xrtc-server` commit `31e351d` |
| 位置 | `src/ice/ice_controller.cpp` — `GetConnectionPingInterval()`（第 163 行） |
| 问题 | 仍保留 `if (weak() || !conn->stable(now))`。按 xrtc-server 的分析，`weak()`=true 时两条到达路径（round-robin 走 `IsPingable`、selected_connection 走 `FindNextPingableConnection`）都在上游被 writable 短路，永远到不了这里，属于死代码。不影响行为，仅不干净。 |
| 修复方式（xrtc-server） | 删除 `weak() ||` 判断（仅保留 `!conn->stable(now)`），并同步修正注释。 |

---

## 三、已修复 / 原本不存在（6 个）

| # | 修复点 | 对应 commit | xrtc-server2.0 现状 | 位置 |
|---|--------|------------|--------------------|------|
| 1 | `now` 类型 `int` → `int64_t` | `48d6144` | ✅ 已是 `int64_t` | `ice/ice_controller.cpp:86` |
| 2 | 补充 `ice_transport_channel::receiving()` 访问器 | `17b6439` | ✅ 已存在 | `ice/ice_transport_channel.h:60` |
| 3 | candidate SDP 缺 `typ` 关键字 | `e790041` | ✅ 已是 `" typ "` | `pc/session_description.cpp:292` |
| 4 | `SetPortRange` 中 `max_port` 误写为 `min_port` | `a4f2e04` | ✅ 已是 `max_port_ = max_port` | `ice/port_allocator.cpp:41` |
| 5 | IO 回调事件掩码 + socket 非阻塞 + SDP `o=-` 笔误 | `d4575ae` | ✅ 全部正确：`GenericIOCb` 用回调参数 `events`（即 revents）；`ConnIOCb` 用 `events & EventLoop::READ`；`NewConn` 有 `SockSetnonblock`+`SockSetnodelay`；SDP 首行为 `"o=- 0 2 IN IP4 127.0.0.1"` | `base/event_loop.cpp`、`server/signaling_worker.cpp:300`、`pc/session_description.cpp:338` |
| 6 | 连接空闲超时 + `list_interaction`→`last_interaction` typo + 移除多余 WARNING | `4995c8a` | ✅ 已正确：字段名 `last_interaction`；`ConnTimeCb`/`ProcessTimeout` 均已实现 | `server/tcp_connection.h:54`、`server/signaling_worker.cpp:239,285` |

---

## 四、附录：全部 fix commit 清单（xrtc-server）

```
5dbdd12  fix: UDPPort::create_connection 替换旧连接时 destroy 旧连接防泄漏   ← 一.5
ce31337  fix: IceConnection update_state() TIMEOUT 后 fail_and_destroy       ← 一.3
4a042b1  fix: k_failed 时同步删除 30s ICE 超时定时器                          ← 一.6
48d6144  fix: select_connection_to_ping 中 now 类型 int → int64_t             ← 三.1
ca590d7  fix: _last_ping_received/_last_data_received 未更新导致 receiving 误判 ← 一.4
837233a  fix: STOP_PUSH/STOP_PULL/ANSWER 无 TCP 响应 + UDP socket fd 泄漏     ← 一.1 + 一.2
17b6439  fix: 遗漏 ice_transport_channel::receiving() 访问器                  ← 三.2
e790041  1.5.32: fix candidate SDP format — add missing 'typ' keyword        ← 三.3
a4f2e04  1.5.21 fix PortAllocator::set_port_range max_port error              ← 三.4
d4575ae  fix: IO 事件回调事件掩码传递错误 + socket 未设置非阻塞               ← 三.5
4995c8a  1.5.112: 消除编译警告 + 补充连接超时 + 移除多余 WARNING 日志          ← 三.6
31e351d  fix: 移除 _get_connection_ping_interval 中的 _weak() 死代码          ← 二.7
```

> 另有 2 个纯文档修正未列入：`e2a78ae`（FAQ 章节引用变更）、`2493af0`（文档图补充）。
