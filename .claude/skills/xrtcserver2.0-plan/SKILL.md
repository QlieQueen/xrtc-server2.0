---
name: "xrtcserver2.0-plan"
description: "消息流程 + lesson-by-lesson 方式教学，参照 xrtc2.0-9.9 课程目录（一节课一个目录），手写实现 1V多（一推多拉）WebRTC 媒体服务"
---

# xrtcserver2.0 实现路线

## 仓库路径

- **你的 xrtc-server2.0**：`/home/ydqun/workspace/webrtc/xrtc-server2.0`
- **参考课程**：`/home/ydqun/workspace/webrtc/xrtc2.0-9.9/`（一节课一个目录，`xrtcserver_v2` 为起点，终点 `9.9`）
- **rtcbase**：`/home/ydqun/workspace/webrtc/rtcbase`（软链 → `rtcbase_v2`）
- **背景笔记**：`/home/ydqun/workspace/webrtc/xrtc-server2.0/note/`

## 与参考项目的差异（重要）

- xrtc-server（Google 风格版）的参考是 **git commit 序列**；本工程的参考课程是**目录快照**，一课一目录
- 每课 = 对比相邻目录的 diff（`diff -rq dirA/src dirB/src` + 关键文件 `diff`）
- 当前 2.0 基线 = `xrtcserver_v2`（1V1 DTLS）+ 已修的 6 个 bug（见 `note/bug-fix-对比报告.md`）

## 教学方式

1. **消息流程驱动**：始终知道"这个消息现在到了哪一步"（PUSH / ANSWER / RTP / RTCP / STOP）
2. **lesson-by-lesson**：按课程目录顺序逐课移植，每课 = 参考目录相对前一目录的增量
3. **Phase 开始前三问**（grill）：
   - 这个 Phase 的背景知识你了解吗？（`note/` 下的对应文档）
   - 你想自己手写还是我逐步指导？
   - 本 Phase 你重点关注什么？

## 课程全景（章节 → 主题 → 1V多 相关性）

| 章节 | 主题 | 课程目录 | 1V多 相关性 |
|------|------|---------|:---:|
| 2.x | 去 DTLS：支持明文 RTP/RTCP 通路（SDP 退化 RTP/SAVPF） | 2.3 ~ 2.5 | transparent 模式基础 |
| 3.x | 自建 RTP/RTCP 模块：RTCPSender + RTCPReceiver | 3.2 ~ 3.11 | **PLI/SR 依赖** |
| 4.x | 视频接收链路 + 接收统计（丢包/jitter/RR/SR/LSR/DLSR/RTT） | 4.1 ~ 4.17 | RR/SR 机制铺垫 |
| 5.x | 视频帧组装：depacketizer → PacketBuffer → 组帧 | 5.1 ~ 5.5 | 接收侧完整化 |
| 6.x | 抗丢包：NACK → RTCP NACK → RTX 重传恢复 | 6.1 ~ 6.6 | 接收侧 QoS |
| **7.x** | **转发架构 + 1V多 核心**：双模式 / multi_pull_streams_ / 一推多拉广播 | 7.3 ~ 7.8 | **主干** |
| **8.x** | PLI 关键帧请求（新拉流端快速出画） | 8.2 ~ 8.3 | **必需** |
| **9.x** | 下行发送链路 + SR 时间线透传（多路音视频同步） | 9.1 ~ 9.9 | **完整版** |

## 当前进度

| Phase | 课程 | 内容 | 状态 | 参考目录 |
|-------|------|------|------|---------|
| 0 | — | 基线对齐（2.0 = v2 + 6 bug 修复，1V1 联调通过） | ✅ done | `xrtcserver_v2` |
| 1 | 2.3 ~ 2.5 | 去 DTLS 明文通路 | ✅ done | `v2_2.3` → `v2_2.5` |
| 2 | 3.2 ~ 3.11 | RTCP 发送/接收模块 | ✅ done | `v2_3.2` → `v2_3.11` |
| 3 | 4.1 ~ 4.17 | 视频接收链路 + 接收统计 | 🔄 进行中（v2_4.1~4.7 done，下一课 v2_4.8） | `v2_4.1` → `v2_4.17` |
| 4 | 5.1 ~ 5.5 | 视频帧组装 | ⬜ todo | `v2_5.1` → `v2_5.5` |
| 5 | 6.1 ~ 6.6 | 抗丢包 NACK/RTX | ⬜ todo | `v2_6.1` → `v2_6.6` |
| 6 | 7.3 ~ 7.8 | **1V多 核心：双模式 + 多拉流 + 广播** | ⬜ todo | `v2_7.3` → `v2_7.8` |
| 7 | 8.2 ~ 8.3 | PLI 关键帧请求 | ⬜ todo | `v2_8.2` → `v2_8.3` |
| 8 | 9.1 ~ 9.9 | 下行 SR 链路 + 时间线透传 | ⬜ todo | `v2_9.1` → `v2_9.9` |

## Phase 1 课程清单（去 DTLS）

| 课 | 目录 | 核心改动 | 知识点 |
|----|------|---------|--------|
| 1 | `v2_2.3` | 信令下发 `is_dtls` 字段，PC/Transport 加 is_dtls_ 开关，关闭时不设证书、SDP 退化 RTP/SAVPF；修 kMeidaProtocolSavpf 拼写 | DTLS-SRTP 可开关、SDP 媒体协议协商 |
| 2 | `v2_2.4` | OnIceState 非 DTLS 时跳过 UpdateState，直接映射 PC 状态 | ICE 状态机 → PC 状态映射 |
| 3 | `v2_2.5` | 补齐非 DTLS 通路：跳过 Dtls/DtlsSrtpTransport，直接用 ICE channel 收发 RTP/RTCP | RTP/RTCP 包类型识别、明文媒体路径 |

## Phase 2 课程清单（RTCP 模块）

| 课 | 目录 | 核心改动 | 知识点 |
|----|------|---------|--------|
| 1 | `v2_3.2` | 新建 modules/rtp_rtcp：RtpRtcpConfig + RtpRtcpImpl 空壳 | 模块分层、配置注入 |
| 2 | `v2_3.3` | 新增 RTCPSender 骨架，RtpRtcpImpl 持有 rtcp_sender_；修 MODULES 宏拼写 | 类组合 |
| 3 | `v2_3.4` | SetRtcpStatus(RtcpMode) + report_flags_ 集合 + ComputeCompoundRTCPPacket 骨架 | RTCP 模式(kOff/kCompound/kReducedSize) |
| 4 | `v2_3.5` | PrepareReport 按 sending_ 生成 SR/RR；IsFlagPresent/ConsumeFlag | SR vs RR 选择、标志消费 |
| 5 | `v2_3.6` | builders_ 函数表(包类型→成员函数) + BuildRR 骨架 | 命令/建造者分派 |
| 6 | `v2_3.7` | PacketSender 辅助类（rtcp::RtcpPacket 序列化进 IP_PACKET_SIZE）；SendRTCP 回调发包 | RTCP 包序列化、复合包组装 |
| 7 | `v2_3.8` | conf 加 rtcp_report_timer_interval；EventLoop 定时器周期触发 RTCP | 定时调度、YAML 配置 |
| 8 | `v2_3.9` | 新增 RTCPReceiver 骨架（IncomingRtcpPacket 入口） | RTCP 接收端设计 |
| 9 | `v2_3.10` | ParseCompoundPacket 用 CommonHeader 逐块解析复合包 | RTCP 复合包格式 |
| 10 | `v2_3.11` | 按 rtcp_block.type() 分派 HandleSr/HandleRr；修 DtlsTransport 信号误接 | SR/RR 处理、signal/slot 接线 |

## Phase 3 课程清单（视频接收链路 + 统计）

| 课 | 目录 | 核心改动 | 知识点 |
|----|------|---------|--------|
| 1 | `v2_4.1` | 新增 video/ 模块骨架：VideoReceiveStream / RtpVideoStreamReceiver / Config | WebRTC 模块分层、框架先行 |
| 2 | `v2_4.2` | PC 收到视频 SDP 后创建 VideoReceiveStream；stream_params 加 FirstSsrc | 一连接一媒体流 |
| 3 | `v2_4.3` | RtpPacketReceived::Parse 解析，按 SSRC 区分音视频路由；记录 video/rtx ssrc | RTP 头解析、SSRC 路由 |
| 4 | `v2_4.4` | 新增 ReceiveStat，OnRtpPacket 记录 ssrc/seq（跳过 recovered） | 接收统计模块化 |
| 5 | `v2_4.5` | ReceiveStat 引入 per-SSRC StreamStat（flat_map 惰性创建） | per-流统计 |
| 6 | `v2_4.6` | StreamStat::UpdateCounters：seq_unwrapper_ 解绕 + 累计丢包 | 序列号回绕、丢包累计 |
| 7 | `v2_4.7` | UpdateOutOfOrder：阈值 450 判定乱序/序列突变 | RTP 乱序处理 |
| 8 | `v2_4.8` | RFC3550 jitter 计算（Q4 定点 jitter_q4_），payload_type_frequency=90k | jitter 公式、Q4 定点 |
| 9 | `v2_4.9` | RTCPSender 搭起：report_flags_ + builders_ + BuildRR；RtpVideoStreamReceiver 建 RtpRtcpImpl(local_ssrc=1) | RTCP 复合包、builder 表 |
| 10 | `v2_4.10` | receive_stat 按多 SSRC 轮询产出 ReportBlock；rtcp_receiver 解析 SR | RTCP ReportBlock、SSRC 轮询 |
| 11 | `v2_4.11` | MaybeAppendReportBlockAndReset 填 fraction lost/cumulative lost/ext seq/jitter | 丢包率计算、ReportBlock 字段 |
| 12 | `v2_4.12` | RR 补 LSR/DLSR；FeedbackState 结构体传 SR 信息 | RTT 测量的 DLSR/LSR |
| 13 | `v2_4.13` | RTCP 投递链：传输层→VideoReceiveStream→RtpVideoStreamReceiver→rtcp_receiver；SetRemoteSsrc 过滤 | RTCP 投递链、SSRC 过滤 |
| 14 | `v2_4.14` | RTCPReceiver 保存 SR 的 NTP/RTP 时间戳，提供 NTP() 接口 | SR 时间戳提取 |
| 15 | `v2_4.15` | GetFeedbackState 汇总 SR 填 FeedbackState；修复 4.12 移位 bug | 反馈状态收集、RTT 前置 |
| 16 | `v2_4.16` | RTCP 上报间隔随机化：audio 5s / video 1s，[1/2,3/2] 抖动，定时器重启 | RTCP 节流随机化 |
| 17 | `v2_4.17` | RtpRtcpModuleObserver 接口，RTCP 包经 OnLocalRtcpPacket 到 PC 再 SendRtcp 给对端 | 观察者模式、RTCP 发送闭环 |

## Phase 4 课程清单（视频帧组装）

| 课 | 目录 | 核心改动 | 知识点 |
|----|------|---------|--------|
| 1 | `v2_5.1` | VideoRtpDepacketizerH264：拆 RTP 载荷，回调 OnReceivedPayloadData | RTP 解封装(depacketizer) |
| 2 | `v2_5.3` | PacketBuffer 缓存乱序 RTP 包，OnInsertedPacket 处理插包结果 | RTP 排序缓冲 |
| 3 | `v2_5.4` | 新增 RtpFrameObject：完整帧（首末 seq/codec/video_header） | 帧对象抽象 |
| 4 | `v2_5.5` | PacketBuffer 组帧 → RtpFrameObject → observer OnFrame → PC::OnFrame | 组帧回调链路 |

## Phase 5 课程清单（抗丢包 NACK/RTX）

| 课 | 目录 | 核心改动 | 知识点 |
|----|------|---------|--------|
| 1 | `v2_6.1` | NackRequester：按序号跟踪丢包 + 乱序直方图统计，经 sigslot 发 NACK 列表 | NACK 丢包检测、逆 CDF |
| 2 | `v2_6.2` | RtpRtcpImpl::SendNack 把 nack_list 传给 RTCP 发送器；OnNackSend 触发发送 | RTCP NACK 通道 |
| 3 | `v2_6.3` | BuildNack 构造 rtcp::Nack 报文（sender/media ssrc + 包 id 列表） | RTCP NACK 报文构造 |
| 4 | `v2_6.4` | 新增 RtxReceiveStream 骨架（media_ssrc/rtx 载荷类型映射/统计） | RTX 接收类设计 |
| 5 | `v2_6.5` | 按 SSRC 分流：rtx_ssrc → RtxReceiveStream，否则媒体接收；从远端 SDP 填 rtx ssrc/apt | RTX 接线与 SSRC 路由 |
| 6 | `v2_6.6` | RTX 解封装：剥 2 字节 RTX 头、还原序号/媒体 PT、标记 recovered 回灌 | RTX 包恢复 |

## Phase 6 课程清单（1V多 核心：双模式 + 多拉流 + 广播）★

| 课 | 目录 | 核心改动 | 知识点 |
|----|------|---------|--------|
| 1 | `v2_7.3` | 新增 transparent/live 传输模式：透传原样转发，直播走媒体处理；信令解析 mode | 双模式架构 |
| 2 | `v2_7.4` | **新增 multi_pull_streams_（stream→uid→PullStream），live 模式注册到多拉流** | **一推多拉数据结构** |
| 3 | `v2_7.5` | RemovePullStreamM 按 uid 清理多拉流，停流/异常路径按模式释放 | 多拉流生命周期 |
| 4 | `v2_7.6` | StopPull/FindPullStream 按 mode 分流 + FindPullStreamM 按 uid 查；信令 stop/update 带 mode | 模式感知查找/停止 |
| 5 | `v2_7.7` | live RTP 上报链：OnRtpPacket(含重传) → PC::SignalRtpPacket → PushStream → manager | RTP 上报链路 |
| 6 | `v2_7.8` | **live OnRtpPacket 把 push RTP 广播给所有拉流端**；枚举 kPush/kPull | **一推多拉广播转发** |

## Phase 7 课程清单（PLI 关键帧请求）

| 课 | 目录 | 核心改动 | 知识点 |
|----|------|---------|--------|
| 1 | `v2_8.2` | is_pli 配置链：is_pli=1 时设 request_pli_interval_ms=2000，定时器周期 SendRTCP(kRtcpPli) | 周期关键帧请求机制 |
| 2 | `v2_8.3` | RTCPSender::BuildPli 真正构造 rtcp::Pli 发出 | RTCP PLI 报文构造 |

## Phase 8 课程清单（下行 SR 链路 + 时间线透传）

| 课 | 目录 | 核心改动 | 知识点 |
|----|------|---------|--------|
| 1 | `v2_9.1` | 新增 VideoSendStream 空壳 + Config | 发送侧流抽象 |
| 2 | `v2_9.2` | PC 加 local_audio/video/rtx_ssrc，CreateOffer 取 send_stream FirstSsrc，实例化 VideoSendStream | 本地发送 SSRC |
| 3 | `v2_9.3` | 新增 RtpSender：按 ssrc 分 rtp/rtx 累计 StreamDataCounters | 发送统计 |
| 4 | `v2_9.4` | RtpRtcpImpl 内嵌 RtpSender；VideoSendStream 建 RtpRtcpImpl(kCompound+sending)；config 加 local ssrc | 统计接入 RTCP、发送端成型 |
| 5 | `v2_9.5` | RTCPSender::BuildSR：GetFeedbackState 汇总 rtp+rtx 计数生成 packets/octets | RTCP Sender Report 构造 |
| 6 | `v2_9.6` | PullStream::SendPacket 解析 RTP→pc→SendRtp+UpdateRtpStat；live 转发改 SendPacket；answer 第2个 ssrc 作 local_video_rtx_ssrc_ | **1V多 RTP 扇出接入统计** |
| 7 | `v2_9.8` | RTCPReceiver 解析上游 SR → OnSrInfo(media_type, rtp_timestamp, ntp) → PC→RtcStream→Manager | 上游 SR 的 RTP↔NTP 采集 |
| 8 | `v2_9.9` | SR 透传闭环：manager OnSrInfo 广播给所有 pull → rtcp_sender.SetSrInfo，BuildSR 复用上游 ts/ntp | 下行 SR 复用上游时间线（多路音视频同步） |

## 参考文件速查

| Phase | 参考目录（在 `xrtc2.0-9.9/` 下）|
|-------|-------------------------------|
| 1 | `xrtcserver_v2_2.3/` ~ `_2.5/`：pc/peer_connection、pc/transport_controller、server/signaling_worker、src/xrtcserver_def |
| 2 | `_3.2/` ~ `_3.11/`：modules/rtp_rtcp/rtcp_sender、rtcp_receiver、rtp_rtcp_impl、rtp_rtcp_config |
| 3 | `_4.1/` ~ `_4.17/`：video/、modules/rtp_rtcp/receive_stat、rtcp_sender、rtcp_receiver |
| 4 | `_5.1/`、`_5.3/`~`_5.5/`：video/rtp_video_stream_receiver、modules/video_coding/rtp_frame_object |
| 5 | `_6.1/` ~ `_6.6/`：modules/video_coding/nack_requester、video/rtx_receive_stream、rtcp_sender |
| 6 | `_7.3/` ~ `_7.8/`：stream/rtc_stream_manager、stream/rtc_stream、stream/push_stream、pc/peer_connection |
| 7 | `_8.2/` ~ `_8.3/`：rtc_stream_manager、push_stream、rtcp_sender、rtp_rtcp_impl |
| 8 | `_9.1/` ~ `_9.9/`：video/video_send_stream、modules/rtp_rtcp/rtp_sender、pull_stream、rtcp_receiver、rtcp_sender |

## 每 Phase 验证

1. 编译通过：`bash build.sh`（0 警告 0 错误）
2. 云机部署 + 客户端联调（见 `note/1V多-学习计划.md` 验证方式）
3. 1V多 专项（Phase 6 后）：一推**多**拉、PLI 快速出画、独立停流不影响他人

## 每 Phase 标准流程

```
1. Grill（三问）
2. 看参考课程对应目录相对前一目录的 diff
3. 逐课移植代码到当前 2.0
4. 编译 + 云机部署 + 客户端联调
5. git commit（你决定时机）
```

## 关键架构提醒

```
消息路径（PUSH / live 模式，1V多）：
  TCP → SignalingWorker → RtcServer(CRC32) → RtcWorker
    → RtcStreamManager::CreatePushStream → PushStream → PeerConnection
      → TransportController → IceAgent → IceTransportChannel → UDPPort

RTP 广播（live）：
  推流端 RTP → UDPPort → IceConnection → Transport → PeerConnection::SignalRtpPacketReceived
    → PushStream(注册到 manager) → RtcStreamManager::OnRtpPacketReceived
      → 遍历 multi_pull_streams_[stream_name] → 每个 PullStream::SendRtp/SendPacket

多拉流数据结构：
  multi_pull_streams_: unordered_map<stream_name, unordered_map<uid, PullStream*>>
  与老 pull_streams_（1V1 单值）按 mode 分流，互不干扰
```

## 背景知识文档

| 文档 | 内容 |
|------|------|
| `note/bug-fix-对比报告.md` | 6 个已修 bug 的对比与说明 |
| `note/1V多-学习计划.md` | 学习计划、课程全景、Phase 目标、验证方式 |

## 常见陷阱

1. `multi_pull_streams_` 是 (stream_name, uid) 二元索引，RTP 广播要遍历同 stream 所有 uid，不能只取一个
2. PullStream 的 RTCP：多个拉流端各自回 RTCP，给 push 的是汇总（OnRtcpPacketReceived 的 pull→push 分支）
3. SetAnswer / StopPull 在多拉流下必须按 (stream_name, uid) 精确定位，uid 校验不能省
4. PLI 走 `rtcp_sender`（Phase 2 产物），当前 2.0 无此模块，Phase 6/7 前必须先补 Phase 2
5. SR 时间线透传（9.9）：下行 SR 复用上游 rtp_timestamp/ntp，保证多路拉流音视频同步一致
6. 课程目录编号不连续（缺 3.1/5.2 等），以实际目录为准
7. CMakeLists 用 `file(GLOB ...)` 自动收编新源文件，新增模块无需改 CMake 的 glob
