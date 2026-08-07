# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

WebRTC 媒体服务 — C++ 实现。通过 XRPC 协议（TCP 9000）接收 `signaling`（Go 信令服务）的推拉流命令，完成 ICE 协商、DTLS-SRTP 传输、媒体流转发。这是 `/home/ydqun/workspace/lession/CLAUDE.md` 所述 Go 信令服务的**南向对端**。

- **上游信令**：`/home/ydqun/workspace/lession/signaling`（Go，HTTP 8080/8081 → XRPC TCP → 本服务）
- **基础库依赖**：`rtcbase`（WebRTC rtc_base 精简分支，提供 `rtc_base/*` 头文件和 `librtcbase.a`），独立仓库：`git@github.com:QlieQueen/rtcbase.git`

## 开发命令

```bash
# 编译（out-of-tree，产物 out/xrtcserver）
./build.sh            # 等价于 cd out && cmake ../ && make
./build.sh clean      # 等价于 make clean

# 运行（在项目根目录，conf 路径为相对路径 ./conf/xxx.yaml）
./out/xrtcserver
```

无 lint / test（项目无测试框架）。

**构建前置**：CMakeLists.txt 的 include/link 路径指向 `../rtcbase/src` 和 `../rtcbase/out`。父目录只有 `rtcbase_v2.0`（`rtcbase_v1.0` 是旧版），构建前需建立符号链接：

```bash
ln -s /home/ydqun/workspace/lession/rtcbase_v2.0 /home/ydqun/workspace/lession/rtcbase
```

链接的库都在 `third_party/lib/*.a`（yaml-cpp、ev、jsoncpp、openssl、srtp2、absl）与 `../rtcbase/out/librtcbase.a`，全部静态链接；因预编译库非 `-fPIC`，链接时加了 `-no-pie`。

### rtcbase 依赖关系（WebRTC 基础组件）

**rtcbase** 是 WebRTC `rtc_base` 的精简分支，本工程 20 个 `rtc_base/*` 头 + 2 个 `api/*` 头全部来自它，**编译和链接都强依赖 `../rtcbase`**：

| 依赖 | 路径 | 说明 |
|------|------|------|
| 头文件 | `../rtcbase/src/rtc_base/*.h`、`../rtcbase/src/api/*.h` | `logging.h`(RTC_LOG)、`rtc_certificate.h`/`rtc_certificate_generator.h`、`time_utils.h`(TimeMillis)、`zmalloc.h`、`slice.h`、`buffer.h`、`copy_on_write_buffer.h`、`socket_address.h` 等 |
| 静态库 | `../rtcbase/out/librtcbase.a`（33MB） | 提供 `rtc::*` 符号实现，CMake `target_link_libraries` 引入 |
| 仓库 | `git@github.com:QlieQueen/rtcbase.git` | 独立维护，需另行 clone/更新 |

rtcbase 目录结构：`src/{rtc_base, api, common_video, modules, system_wrappers}`。若 `../rtcbase` 不存在或与版本不匹配，整个工程编译失败；升级 rtcbase 后需重编 `librtcbase.a` 并同步 `third_party/include` 中的对应头文件。

## 架构

单进程、双 server 模型，全部基于自研 **EventLoop**（epoll 封装，`src/base/event_loop.{h,cpp}`，IO/Timer/Notify watcher + pipe 线程间通信）：

```
                 ┌────────────────────────── xrtcserver ──────────────────────────┐
                 │                                                               │
   signaling(Go) │  SignalingServer (TCP 9000)          RtcServer (媒体)          │
   HTTP push/pull│  └─ SignalingWorker×N ──RtcMsg──→ ──┐  └─ RtcWorker×N          │
   ─── XRPC ────►│  XRPC 解析/应答              SendRtcMsg  │  RtcStreamManager     │
                 │  offer 原路写回                        │  Push/PullStream        │
                 │                                        │  PeerConnection         │
                 └────────────────────────────────────────┴────────────────────────┘
```

### 消息流转（与信令侧 CMDNO 对齐，见 `src/xrtcserver_def.h`）

```
1. signaling 发 TCP 请求：36 字节 xhead + JSON body（cmdno/uid/stream_name/audio/video/sdp）
2. SignalingWorker::ProcessRequest 校验 magic_num、解析 JSON，构造 RtcMsg 投递给 g_rtc_server
3. RtcServer 生成/复用 DTLS 证书（GenerateAndCheckCertificate），按 CRC32(stream_name) % worker_num
   选 RtcWorker（rtc_server.cpp:218 GetWorker）
4. RtcWorker 用 RtcStreamManager 建/查 PushStream、PullStream
5. PushStream::CreateOffer() → PeerConnection::CreateOffer() 生成 SDP offer
6. offer 经 RtcMsg 原路送回 SignalingWorker（msg->worker/conn/fd），以
   {"err_no","err_msg","offer"} JSON 写回同一 TCP 连接
7. answer 走 CMDNO_ANSWER → RtcStreamManager::SetAnswer() → SetRemoteSdp() → DTLS 握手 → RTP 流
```

### 核心模块

| 目录 | 职责 |
|------|------|
| `src/server/` | **signaling_server**：TCP 服务 + worker 分发；**signaling_worker**：XRPC 36B 头 + JSON body 解析、按 cmdno 分派、offer 响应写回；**rtc_server**：证书管理 + `CRC32(stream_name)%N` 选 worker；**rtc_worker**：建流/设 answer；`tcp_connection`：连接态（querybuf/reply_list/watchers） |
| `src/stream/` | `RtcStream` 基类；`PushStream`（入流，CreateOffer）；`PullStream`（订阅 PushStream 的 audio/video source）；`RtcStreamManager`（按 stream_name 索引；pull 必须已存在对应 push） |
| `src/pc/` | `PeerConnection`（CreateOffer/SetRemoteSdp、AddAudioSource/AddVideoSource）；SDP 解析生成（session_description）；DTLS-SRTP 传输（dtls_transport/srtp_transport） |
| `src/ice/` | 完整 ICE 栈：ice_agent、ice_transport_channel、port_allocator、udp_port、stun 编解码、ice_credentials、candidate、ice_controller |
| `src/base/` | EventLoop、socket 封装、YAML 配置加载（conf.cpp）、日志（log.cpp，`RTC_LOG` 来自 rtc_base）、`lock_free_queue.h`（线程消息队列）、`xhead.h`（36B 头定义） |
| `src/modules/rtp_rtcp/` | RTP 工具函数 |
| `third_party/` | 预编译头文件 + 静态库（openssl/srtp2/libev/jsoncpp/yaml-cpp/absl） |

### 线程模型

- 主线程：启动两个 server，`Start()` 后 `Join()`；SIGINT/SIGTERM 触发优雅停机（main.cpp:ProcessSignal）。
- **SignalingServer 线程**：accept 后按 round-robin 把 fd 投给某个 SignalingWorker（各自独立 event loop + pipe notify）。
- **RtcServer 线程**：单一主 event loop，只做消息路由 + 证书生成，不建流。
- **RtcWorker×N 线程**：真正的媒体处理（建流、ICE、DTLS），stream_name 哈希到固定 worker，保证同流消息有序。
- 跨线程通信统一走 **pipe + Notify(枚举)**：队列用 `lock_free_queue.h`，`RtcMsg`（含 `worker`/`conn`/`fd` 指针）跨线程传递。

## 配置文件

```bash
# 所有路径都是相对启动目录的，必须从项目根目录运行 ./out/xrtcserver
conf/general.yaml         # 日志目录/级别 + ICE 网卡/地址/端口区间
conf/signaling_server.yaml# TCP 监听 host:port(9000)、worker_num、connection_timeout(us)
conf/rtc_server.yaml      # 媒体 worker 数
```

关键点：`general.yaml` 中 **云服务器部署要填公网 `ipv4_addr`**（网卡扫描拿到的是内网 IP），本地联调留空自动扫描网卡；`min_port`~`max_port` 是 ICE host candidate 的 UDP 端口区间。

## XRPC 协议（与 `xhead.h` 及 Go 侧 `src/framework/xrpc/header.go` 对齐）

```
Byte Offset  0        2        4        8               24       28       32       36
            ┌────────┬────────┬────────┬────────────────┬────────┬────────┬────────┐
            │ Id     │Version │ LogId  │ Provider[16]   │MagicNum│Reserved│ BodyLen│
            └────────┴────────┴────────┴────────────────┴────────┴────────┴────────┘
```

- MagicNum: `0xfb202202`（小端），XHEAD_SIZE=36，多字节字段一律 **Little Endian**
- 请求 body 为 JSON：`{"cmdno","uid","stream_name","audio","video"}`（ANSWER 多 `sdp`）
- 响应 body 为 JSON：`{"err_no","err_msg","offer"}`（offer 仅 PUSH/PULL 成功时非空）
- 每个 TCP 连接是**短连接**，处理完一个请求即关闭（signaling_worker.cpp:ProcessQueryBuffer 设 `bytes_processed = 65535`）

## 关键数据流（以 PUSH 为例）

```
signaling --XRPC--> SignalingWorker::ProcessPush
  → RtcMsg{cmdno:1, uid, stream_name, audio, video, conn, fd}
  → RtcServer::SendRtcMsg → GetWorker(CRC32(stream_name)%N)
  → RtcWorker::ProcessPush → RtcStreamManager::CreatePushStream
  → PushStream::CreateOffer → PeerConnection::CreateOffer
  → 生成 SDP offer（含 ICE ufrag/pwd + DTLS fingerprint）
  → 原路返回 SignalingWorker::ResponseServerOffer → 写回 TCP
  → signaling 把 offer 返回给 VS2022 客户端
  → 客户端 createAnswer → /signaling/sendanswer → CMDNO_ANSWER
  → RtcWorker::ProcessAnswer → SetAnswer → SetRemoteSdp → ICE 连通 → RTP 流
```
