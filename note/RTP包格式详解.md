# RTP 包格式详解

> WebRTC 媒体传输的最小单元，SFU 转发、接收统计、SDP 协商全都在 RTP 包这一层做文章。
> 本文对照 rtcbase 的 `rtp_packet.h` / `rtp_packet_received.h` 解析逻辑写，字段/偏移与代码一致。

## 一、RTP 包总览

RTP 包 = **12 字节固定头 + 可选 CSRC + 可选扩展头 + 载荷（Payload） + 可选 Padding**：

```
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            Contributing source (CSRC) identifiers             |
// |                             ....                              |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |  header eXtension profile id  |       length in 32bits        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Extensions                           |
// |                             ....                              |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |                           Payload                             |
// |             ....              :  padding...                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |               padding         | Padding size  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

## 二、12 字节固定头（每个 RTP 包必有）

| 偏移 | 字段 | 位数 | 说明 |
|------|------|:---:|------|
| 0 | **V（Version）** | 2 | 版本号，WebRTC 恒为 **2** |
| 0 | **P（Padding）** | 1 | 1 = 包尾带 Padding，最末 1 字节是 Padding 长度 |
| 0 | **X（eXtension）** | 1 | 1 = 固定头之后跟扩展头 |
| 0 | **CC（CSRC Count）** | 4 | CSRC 个数（0~15），固定头后有 CC 个 CSRC |
| 1 | **M（Marker）** | 1 | 标记位，视频通常"关键帧的最后一个分片"置 1 |
| 1 | **PT（Payload Type）** | 7 | 载荷类型，96~127 是动态类型（WebRTC 用它映射 codec） |
| 2 | **Sequence Number** | 16 | 序号，**每发一个包 +1**，接收端靠它排序/测丢包（会回绕） |
| 4 | **Timestamp** | 32 | 时间戳（采样频率 tick），视频 90000Hz，音频 48000Hz |
| 8 | **SSRC** | 32 | 同步源标识，一条流的"身份证"，接收端按它认流 |

### 固定头三个字段在 SFU 里的用途（对照代码）

```cpp
// rtp_packet.h 提供的访问器，本工程 4.3 起在 PeerConnection::OnRtpPacketReceived 用到
uint16_t SequenceNumber() const;   // 排序、测丢包（4.x 接收统计）
uint32_t Timestamp() const;        // jitter 计算（配合 arrival_time）
uint32_t Ssrc() const;             // 认流 → GetMediaType(ssrc) 分拣音视频
uint8_t PayloadType() const;       // 识别 codec / 区分 RTP vs RTCP
bool Marker() const;               // 帧边界（关键帧起始）
```

### 本工程怎么区分 RTP / RTCP（复用同一个 V=2 + 序号区间的判断）

`rtp_utils.cpp` 的 `InferRtpPacketType`：

```cpp
bool PayloadTypeIsReservedForRtcp(uint8_t payload_type) {
    return 64 <= payload_type && payload_type < 96;   // RTCP 用 64~95
}
bool IsRtpPacket(ArrayView packet) {
    return packet.size() >= 12 &&                       // 至少固定头长
        HasCorrectRtpVersion(packet) &&                 // V == 2
        !PayloadTypeIsReservedForRtcp(packet[1] & 0x7F); // PT 不在 64~95
}
```

> **关键认知**：RTP 和 RTCP 都走同一个 ICE UDP channel，两者靠**第一个字节的高 2 位都是 2**（共同），再靠**第二字节的低 7 位（PT）是否落在 64~95** 区分。非 DTLS 模式下 `OnReadPacket` 就是靠这个分流的。

## 三、CSRC（可选，CC > 0 时才有）

```
+ += += += += += += += += += += += += += += += += += += += += += += += +
|            Contributing source (CSRC) identifiers                  |
|                             ....                                  |
```

- CC 字段告诉你有几个 CSRC，每个占 4 字节，紧跟固定头
- 语义：**这个 RTP 包由哪些源混合而来**（语音会议混音场景才有）
- WebRTC 单对单 / SFU 转发场景**不用 CSRC**（CC = 0），所以固定头就是整整 12 字节
- rtcbase 里 `SetCsrcs()` 写入、`Csrcs()` 读取，当前工程不涉及

## 四、扩展头（可选，X = 1 时才有）

```
+ - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - +
|  header eXtension profile id  |       length in 32bits            |
+ - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - +
|                          Extensions                               |
|                             ....                                  |
```

- 格式：**4 字节头**（2 字节 profile id + 2 字节扩展总长度，单位是 32-bit 字）+ 扩展数据
- **RFC 5285 两种格式**（`one-byte / two-byte`），WebRTC 用 one-byte：扩展项格式为 `[id:4 | len:4] + 数据`，id 1~14 是 RTP header extension
- 常见扩展（WebRTC 标准注册的）：
  - **Transport-wide cc**（id=3，transport-cc）：带宽估计 / 拥塞控制用
  - **abs-send-time**（id 可选）：NTP 时间戳，用于接收端判断网络延迟
  - **RTP MID**（`a=mid` 关联，BUNDLE 复用一条通道时的 m-line 区分）
  - **RTP Stream-id / Repaired-RTP-stream-id**（sdes-mid / rtx 恢复流标识）
- rtcbase 用 `ExtensionManager` 管理 id↔type 映射，`IdentifyExtensions()` 注册后 `GetExtension()` 读取
- **SFU 的立场**：转发通常**原样透传**扩展头（不去 parse 每个扩展），除非做带宽估计或 SSRC 重写才需要动它

## 五、Payload（载荷）

```
+ - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - +
|                           Payload                                 |
|             ....              :  padding...                       |
+ - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - +
|               padding         | Padding size                      |
+ - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - + - +
```

- 固定头 + CSRC + 扩展头之后就是载荷
- 载荷**本身不含编码信息**（codec 靠 SDP 里 PT 映射），也不管帧边界
- 视频 payload 通常是 **RTP 封装后的 H264 NAL 单元**（单个 NAL、或 NAL 分片 FU-A/FU-B，见 5.x 课程）
- rtcbase 里：
  ```cpp
  size_t payload_size() const;                 // 载荷长度
  ArrayView<const uint8_t> payload() const;    // 载荷视图（指向 buffer 内部，非拷贝）
  ```

## 六、Padding（可选，P = 1 时才有）

- 包尾补齐字节 + **最后 1 字节 = Padding 长度（含自身）**
- rtcbase：`has_padding()`（查第 1 字节 bit 5）、`padding_size()`、`SetPadding()`
- 用途：块对齐 / 加密填充（SRTP 有时用），接收端解析时**要知道它并在算载荷时减掉**
- rtcbase 的 `payload_size()` 返回的是**不含 padding 的净载荷**，解析时已自动处理

## 七、本工程数据流里 RTP 的完整生命周期（对照代码）

```
ICE channel 收到 UDP 包（RTP 或 RTCP）
  → TransportController::OnReadPacket
  → InferRtpPacketType() 判 RTP/RTCP → SignalRtpPacketReceived
  → PeerConnection::OnRtpPacketReceived (4.3)
  → RtpPacketReceived::Parse(move(buffer))     // 解析出所有头部字段
  → set_arrival_time()                          // 记录到达时间（jitter 基准）
  → GetMediaType(Ssrc())                        // 按 SSRC 认流
  → video_receive_stream_->OnRtpPacket()        // 投递进接收流
  → rtp_video_stream_receiver_.OnRtpPacket()    // 4.4 起在此做接收统计
```

之后（5.x 起）接收统计会用到：
- **SequenceNumber()**：序列号回绕检测、丢包计数
- **Timestamp() + arrival_time()**：jitter 计算（到达间隔 vs 采样间隔）
- **Marker()**：识别帧边界（关键帧请求 PLI 后"出画"判断）

## 八、与 RTCP 的关系（放在一起理解）

| | RTP | RTCP |
|---|---|---|
| 传输内容 | 媒体数据（音视频） | 控制信息（SR/RR/NACK/PLI…） |
| 发送频率 | 高频（每帧若干包） | 低频（周期 100ms 级 / 事件触发） |
| 端口 | 与 RTCP 共用（rtcp-mux）或独立 UDP 端口 | 同左 |
| 区分方式 | 第二字节 PT 不在 64~95 | 第二字节 PT 在 64~95 |
| 是否加密 | SRTP | SRTCP（DTLS 模式） |

> **一句话总结**：RTP 头 12 字节固定，SSRC 认流、SequenceNumber 排序测丢包、Timestamp 配 arrival_time 算 jitter，扩展头承载带宽估计等辅助信息——SFU 转发最关心的是**不改 SSRC / 不重编载荷**，其余字段原样透传。
