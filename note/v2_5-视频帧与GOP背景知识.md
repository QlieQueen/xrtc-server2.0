# v2_5 视频帧与 GOP 背景知识

> 对应课程：`xrtcserver_v2_5.1` ~ `v2_5.5`（视频帧组装：depacketizer → PacketBuffer → RtpFrameObject → 组帧）
> 状态：背景笔记（2026-08-14 首次；同日补 AU 概念），代码从 5.1 起逐步落地
> 定位：**5.x 的物理前提**。5.x 的代码（PacketBuffer 组帧）背后全是视频编码层概念：分片、GOP、I/P 帧、帧内丢包、帧间预测。先打通背景，写代码才不迷路
> 代码落点：5.1 depacketizer / 5.3 PacketBuffer / 5.4 RtpFrameObject / 5.5 组帧回调；6.x NACK（包级）；8.x PLI（帧级）
> 双线贯通：作者另有 **手写客户端（推流端，不用 libwebrtc）** 课程线，手写 NALU → RTP **打包**；本篇第六节把它与本工程 RTP → NALU **拆包** 串成镜像对照
> 配套：`RTP包格式详解`（RTP 头）、`v2_4.x`（接收统计）、`v2_4-RR包格式详解`

## 〇、Access Unit（AU）：比特流里的"帧"

### 0.1 定义：一帧画面的 NAL 集合

H.264/H.265 规范：**AU = 一组 NAL unit 的集合，解码这组 NAL 输出一幅完整画面**（primary coded picture）。

三层对应：

```
编码器输入：一幅图像（pixel 层面）
编码器输出：一个 AU（bitstream 层面）= 一组 NAL
解码器输出：一幅图像（回到 pixel 层面）
```

**讨论比特流 / WebRTC 时，口头的"一帧" = 一个 AU。**

### 0.2 经典 AU 结构

```
[AU 边界]
  SPS NALU        (non-VCL，序列参数)
  PPS NALU        (non-VCL，图像参数)
  SEI NALU        (non-VCL，可选，辅助信息)
  IDR slice NALU  (VCL，真正的图像数据)
[AU 边界]
```

普通 P 帧的 AU 只有 slice NALU——**一帧 = 一个 AU = 一个 slice NALU 是默认形态**；4K 大帧可切成多个 slice，AU 里就是多个 slice NALU。

### 0.3 两个精确点

- **SPS/PPS 可内可外**：规范允许带内（和 IDR 一起打进 AU）或带外单独传（如 SDP `sprop-parameter-sets`）。实际编码器（x264/FFmpeg）经典做法是**打进每个 IDR AU 的前面**。打包时同一 AU 的 NAL 按"**小聚合、大分片**"装进该帧的 RTP 包：小的完整 NAL（SPS/PPS/SEI）→ **STAP-A** 聚合；大的 NAL（通常是 IDR slice）→ **FU-A** 分片（STAP-A 要求 NAL 完整，装不下大 IDR）。
- **VCL vs non-VCL**：slice 是 VCL（真正的图像编码数据，出图靠它），SPS/PPS/SEI 是 non-VCL（让解码器"知道怎么解"的辅助数据）。

### 0.4 与 RTP 的对应：timestamp = AU 边界

```
一个 AU ──打包──> 一组同 timestamp 的 RTP 包（seq 递增，末包 M=1）
一个 timestamp 值 = 一个 AU —— RTP 层划 AU 边界的依据
```

下面第一节讲的"一帧被切成多个 RTP 包"，里面的"一帧"指的就是一个 AU。

### 0.5 概念链闭环

```
画面(pixel) → 编码 → AU(NAL集合) → 打包 → RTP包(同timestamp, M收尾)
   ↑                                             ↓
画面(pixel) ← 解码 ← AU(NAL集合) ← 组帧 ← RTP包（PacketBuffer 5.3~5.5）
```

> 一句话：**AU 是比特流里的"帧"——一帧 = 一个 AU = 一组 NAL；经典 IDR AU = SPS + PPS + IDR slice；RTP 层用 timestamp 认 AU 边界。**

## 一、一帧 → 多个 RTP 包：为什么、怎么判断

### 1.1 为什么分片

一个视频帧（尤其 I 帧）动辄几十 KB，而 RTP 一个包的载荷上限约 1200 字节（受 MTU 限制），所以**一帧必然被切成多个 RTP 包**：

```
I帧(40KB) ──切成──> RTP包 #1..#33，共享同一个 timestamp，最后一个包 M=1
```

### 1.2 判定"哪些包属于同一帧"的三个判据

| 判据 | 作用 |
|------|------|
| **RTP timestamp** | **同帧的包共享同一个 timestamp**——帧级分组的关键。timestamp 一变 = 新的一帧开始 |
| **sequence number** | 帧内包的顺序（解绕后连续）。**缺口 = 丢包** |
| **M 位 (marker)** | 视频里 M=1 标记这一帧的**最后一个包**（帧边界） |

**一帧的判定** = 收集 timestamp 相同的一组包 → 按 seq 排序 → 检查 seq 是否连续 → 用 M 位确认帧完整收尾。

### 1.3 丢包检测 = seq 缺口

```
I帧 33 个包，第 5 个丢了：
同 timestamp 收齐后发现 seq 序列 #4 后直接跳到 #6 → 缺口 = 丢包，帧不完整
```

## 二、GOP / I 帧 / P 帧：编码结构

```
GOP = [ I帧 ] [ P帧 ] [ P帧 ] ... [ P帧 ] [ I帧 ] [ P帧 ] ...
      └──────────── 一个 GOP ────────────┘
```

| 帧类型 | 编码方式 | 依赖 | 体积 |
|--------|---------|------|------|
| **I 帧（关键帧）** | 帧内编码，完整编码整幅画面 | 不依赖任何帧，**可独立解码** | 大（P 帧的 5~20 倍） |
| **P 帧** | 帧间预测，只编码"与参考帧的差异 + 运动矢量" | **依赖前面的参考帧**（I 或更早的 P） | 小 |

**关键：P 帧的参考链是一条链**——第 3 个 P 帧依赖第 2 个，第 2 个依赖第 1 个，第 1 个依赖 I 帧：

```
I → P1 → P2 → P3 → P4
```

## 三、帧内丢包：一帧缺一片 = 整帧完蛋

场景：I 帧 33 个包里第 5 个丢了。PacketBuffer 按 timestamp 收齐同帧包时发现 seq 缺口 → **帧不完整 → 整帧丢弃**。

> **关键认知：一帧少一包 = 整帧丢弃，不是"只坏一部分"。** 解码器要的是完整 NAL，缺一个分片拼不出完整的帧，且残缺帧还会污染参考链。

丢的包怎么办？两条路：
- **NACK 重传（6.x）**：向对端要这个 seq 的包。但有 RTT 延迟——**赶得上这一帧的解码时刻就等，赶不上就放弃这帧**
- **放弃 + PLI**：等下一帧，或主动请求关键帧（见下）

## 四、帧间预测的连锁反应：比"丢一帧"狠得多

参考链断裂：

```
I → P1 → P2 → P3 → P4
      ↑丢了P1
       ↓
P2/P3/P4 全部参考断裂 → 连锁解码失败，直到下一个 I 帧才恢复
```

**"丢包率不高但画面一卡卡一串"的真相**：参考链一断，断点之后整条链都废了，要等 GOP 边界（下一个 I 帧）才能重建。I 帧间隔（GOP 大小）通常 1~2 秒——**一次关键帧丢失，画面可能卡 1~2 秒**。

**应对 = PLI（Picture Loss Indication，8.x）**：接收端发现解码错误/丢帧，**主动向发送端要一个新的 I 帧**，快速重建参考链，不用干等下一个 GOP。这是 8.x `BuildPli` 在做的事。

## 五、NACK vs PLI：包级 vs 帧级（5.x 到底为谁服务）

### 5.1 本质区别

| | NACK (6.x) | PLI (8.x) |
|------|-----------|-----------|
| **请求对象** | 某个 seq 的 **RTP 包** | 新的 **关键帧（I 帧）** |
| **层级** | **包级**（seq 缺口检测） | **帧级**（帧完整性/参考链判断） |
| **目的** | 恢复丢失的单包 | 重建断裂的参考链 |
| **需要组帧（5.x）吗** | **不需要** | **需要**（帧完整性 + 帧类型判断） |

### 5.2 代码证据（参考课程查证）

1. **5.5 组帧后只打日志**：`PeerConnection::OnFrame` 只有一行 `RTC_LOG`（打印 frame_type + seq 范围），SFU **不消费帧**——它转发的是 RTP 包（7.x RTP 上报链）
2. **6.x NACK 独立于组帧**：`NackRequester` 在 `modules/video_coding/` 独立存在，基于 seq 缺口，与 PacketBuffer/帧无关
3. **8.x PLI 的燃料是 PacketBuffer**：8.2 里 `RtpVideoStreamReceiver` 持有 `packet_buffer_`，每包 `InsertPacket` → 组帧。PLI 判断"帧收齐了没、丢的是不是关键帧、参考链断没断"，全靠 PacketBuffer（5.3~5.5）

### 5.3 SFU vs MCU：5.x 在 SFU 里的真实定位

```
MCU：接收 → 组帧 → 解码 → 再编码 → 转发   (帧层面必须完整，5.x 是硬需求)
SFU：接收 → 转发 RTP 包                  (帧层面是可选理解，不是硬需求)
```

**结论**：在纯转发 SFU 里，5.x 的组帧确实不是必需（transparent 模式原样透传 RTP 包）。它存在的三条真实价值：

1. **8.x PLI 的判断燃料**（最主要）——PLI 不能无脑发，要判断"现在该不该请求关键帧"，这需要帧完整性 + 帧类型，来自 PacketBuffer
2. **接收端链路的完整性**——你学的是标准 WebRTC 接收端（拆包 → 统计 → 组帧 → 解码前），5.x 补全这环，链路才完整
3. **理解 RTP 载荷结构**——为什么一帧多包、为什么丢一包坏一帧、FU-A 怎么重组。SFU 排障时这些认知价值连城

> **一句话**：你学的 5.x 是一个"完整 WebRTC 接收端"里的一环，SFU 只用到它的部分能力（统计做 RR/NACK，帧判断做 PLI）。理解了完整链路，才真正懂 SFU 为什么保留 packet_buffer、为什么有 NACK/PLI、转发时怎么决策。

## 六、打包 / 拆包镜像对照：RTP 载荷结构一次贯通

> 背景：作者课程还有一条 **手写客户端（推流端，不用 libwebrtc）** 线，手写实现了 NALU → RTP 的**打包**（序列化）。本笔记讲的是 **接收端** 的**拆包**（反序列化，5.1 depacketizer）。两者互为镜像——**推流端怎么切碎的，接收端就怎么拼回**。当年在推流端手写过的每一个字节，都会在接收端被反着用一遍。

### 6.1 三种封装的双向对照

| 封装 | 打包（推流端：NALU → RTP） | 拆包（接收端：RTP → NALU） |
|------|--------------------------|---------------------------|
| **单 NALU** | 直接塞进 RTP，载荷头 = NAL 头（type 1~23） | 直接取出，载荷头就是 NAL 头 |
| **STAP-A (24)** | 多个小 NALU（SPS/PPS/SEI）前各加 **2 字节长度**，拼进一个包 | 按 2 字节长度字段**逐个切出** NALU |
| **FU-A (28)** | 大 NALU 切成 ≤MTU 的片，每片加 FU indicator + FU header，**首片 S=1、末片 E=1** | 按 seq 收齐分片，**首片重建 NAL 头**（`fnri \| original_nal_type`），丢 FU 头，按序拼接 |

### 6.2 镜像的关键：打包时"故意拆开"的，拆包时"原样拼回"

| 信息 | 打包时放哪 | 拆包时怎么还原 |
|------|-----------|---------------|
| **F/NRI/type** | F+NRI 放 **FU indicator**，type 放 **FU header**——故意拆开 | 首片重建：`rtp_payload[0] = fnri \| original_nal_type`（depacketizer 源码里的重组，5.1 你拿到的 `video_payload` 首字节已是拼好的完整 NAL 头） |
| **S/E 位** | 写：首片 S=1、末片 E=1 | 读：S=1 → 该片是首片、重建 NAL 头；E=1 → 分片结束 |
| **长度字段** | STAP-A 打包时写 2 字节大端 | STAP-A 拆包时按长度逐个切出 |

### 6.3 两端对称收尾

```
打包（推流端）：一个 NALU ──切──> 多个 RTP 包（同 timestamp，seq 递增，末片 M=1）
拆包（接收端）：多个 RTP 包 ──拼──> 一个 NALU（收齐分片 → 重建 NAL 头 → 按序拼接）
```

> 一句话：当年手写推流端切碎的每一个字节，都在接收端被原样倒放——熟悉感不是错觉，是同一份知识换了个方向。

### 6.4 FU-A 包格式（字节级）

RTP 载荷部分 = **两字节头 + 分片数据**：

```
  byte 0           byte 1          byte 2...
+---------------+----------------+----------------------+
| FU indicator  |  FU header     |  FU payload          |
| 1 字节         |  1 字节         |  分片数据 (可变)      |
+---------------+----------------+----------------------+

byte 0  FU indicator:
  0 1 2 3 4 5 6 7
 +-+-+-+-+-+-+-+-+
 |F|  NRI  | TYPE |   TYPE = 28 (0x1C) → 告知接收端"这是分片包"
 +-+-+-+-+-+-+-+-+

byte 1  FU header:
  0 1 2 3 4 5 6 7
 +-+-+-+-+-+-+-+-+
 |S|E|R|  TYPE   |   TYPE = 原始 NAL type（5=IDR, 1=非IDR）
 +-+-+-+-+-+-+-+-+
```

| 字段 | 掩码 | 位置 | 含义 |
|------|------|------|------|
| F | `kFBit = 0x80` | byte0 bit7 | 禁止位（正常 0） |
| NRI | `kNriMask = 0x60` | byte0 bit6-5 | 重要性 |
| **TYPE=28** | `kTypeMask = 0x1F` | byte0 bit4-0 | 载荷格式：FU-A |
| **S (Start)** | `kSBit = 0x80` | byte1 bit7 | **首片标记** |
| **E (End)** | `kEBit = 0x40` | byte1 bit6 | **末片标记** |
| R | `kRBit = 0x20` | byte1 bit5 | 保留位（必须 0） |
| **原始 type** | `kTypeMask = 0x1F` | byte1 bit4-0 | 被切碎的 NALU 真身类型 |

S/E 位的三种组合：

| 片 | S | E | 语义 |
|----|---|---|------|
| **首片** | 1 | 0 | 分片开始，重建 NAL 头靠它 |
| **中间片** | 0 | 0 | 中间段，纯数据 |
| **末片** | 0 | 1 | 分片结束（帧边界靠 M 位，见 6.5） |

**源码逐行**（rtcbase `VideoRtpDepacketizerH264::ParseFuaNalu`）：

```cpp
uint8_t fnri = rtp_payload.cdata()[0] & (kFBit | kNriMask);    // byte0 取 F+NRI
uint8_t original_nal_type = rtp_payload.cdata()[1] & kTypeMask; // byte1 取原始 type
bool first_fragment = (rtp_payload.cdata()[1] & kSBit) > 0;    // 读 S 位

if (first_fragment) {
    // ① 砍掉 byte0，剩下 byte1(FU header) 顶到最前
    rtp_payload = rtp_payload.Slice(kNalHeaderSize, size - kNalHeaderSize);
    // ② 用 fnri | original_nal_type 覆盖 → 重建完整 NAL 头
    uint8_t original_nal_header = fnri | original_nal_type;
    rtp_payload.MutableData()[0] = original_nal_header;
    // 结果 video_payload = [重建的 NAL头 | 首片数据]
} else {
    // 非首片：直接砍 2 字节头，纯分片数据
    parsed_payload->video_payload =
        rtp_payload.Slice(kFuAHeaderSize, size - kFuAHeaderSize);
    // 结果 video_payload = [分片数据]（无 NAL 头）
}

// 帧类型 + 首片标记（5.3 PacketBuffer 组帧的入列依据）
if (original_nal_type == H264::NaluType::kIdr) {
    frame_type = kVideoFrameKey;
} else {
    frame_type = kVideoFrameDelta;
}
is_first_packet_in_frame = first_fragment;
```

**5.1 `OnReceivedPayloadData` 拿到的 `codec_payload` 首字节的来历**：首片进来时是被重建的 NAL 头（如 IDR 的 `0x65`）；非首片直接是码流数据。

拆分 / 拼接镜像：

```
打包（推流端）：一个大 NALU（含 NAL 头 1 字节 + 数据）切成 N 段，
              每段加 2 字节头 [F|NRI|28][S|E|0|原始type]，首片 S=1、末片 E=1，
              全部片共享同一 timestamp、seq 递增、末片 M=1
拆包（接收端）：按 seq 顺序拼接
              首片   video_payload: [重建 NAL头 | 数据]    ← fnri|type 拼回
              中间片 video_payload: [数据]                  ← 纯砍头
              末片   video_payload: [数据]
              拼接 = [NAL头 | 片1 | 片2 | ... | 片N] = 完整 NALU
```

### 6.5 为什么 depacketizer 不读 E 位？——帧级边界靠 M 位

疑问：`ParseFuaNalu` 只处理了 S 位（`first_fragment`），E 位完全没用到，末片不需要处理吗？

答案：**帧边界判断不用 E 位，用 RTP 头的 M 位（Marker）**。证据在 5.3 参考代码：

```cpp
// xrtcserver_v2_5.3/src/video/rtp_video_stream_receiver.cpp
void RtpVideoStreamReceiver::OnReceivedPayloadData(...)
{
    auto packet = std::make_unique<PacketBuffer::Packet>(rtp_packet, video);
    webrtc::RTPVideoHeader& video_header = packet->video_header;
    video_header.is_last_packet_in_frame |= rtp_packet.Marker();  // ← 帧结束靠这个
    OnInsertedPacket(packet_buffer_->InsertPacket(std::move(packet)));
}
```

`is_last_packet_in_frame` **不是 depacketizer 填的，是 RtpVideoStreamReceiver 用 M 位补的**。depacketizer 只管到"是不是首片"，帧边界是更高一层的事。

**两个不同层级的边界标记**：

| 标记 | 在哪个头里 | 标记什么 | 发送端何时置 1 |
|------|-----------|---------|--------------|
| **E 位** | FU-A 头（载荷里） | **一个 NALU** 的最后一个分片 | 一个大 NALU 切完最后一片时 |
| **M 位** | RTP 头（12 字节标准头） | **一帧** 的最后一个 RTP 包 | 一帧的最后一个包时 |

**一帧 ≠ 一个 NALU**（关键帧常见：SPS+PPS+IDR 分片）：

```
帧 = SPS → PPS → IDR片1 → IDR片2 → IDR片3 → SEI
      │      │      │                E=1      M=1
      └──────┴──────┴────────────── 这些 M=0
```

- IDR片3：E=1（IDR 这个 NALU 分完了），但 M=0（帧还没完，后面还有 SEI）
- SEI：M=1（这才是帧的最后一个包）
- 常见场景（一个 NALU = 一帧）下末片恰好是帧末，E、M 同时置 1——发送端切最后一片时"顺手"把 M 也标上

**为什么 M 位能组帧、E 位不行**：

- M 位在**标准 RTP 头**（12 字节固定部分）里，与载荷格式无关 → PacketBuffer 组帧**不用懂 H.264 / 不用管 FU-A**，只按 `timestamp` 分组、`seq` 判连续、`M` 位收尾
- depacketizer 是**单包处理**，不知道 NALU 还有没有下一片；重组拼接按 seq 顺序追加，拼完自然知道，不需要"预告片数"（类比：拼图不需要知道哪片是最后一片，拼到没有下一片就是拼完）
- E 位的价值在排障（收到 E=1 但 seq 有缺口 → 丢片）和协议完整性，不是运行时的必要判断

> 一句话：**E 位 = NAL 级边界，M 位 = 帧级边界；组帧要的是帧，所以只认 M 位。** depacketizer 不读 E 位不是遗漏，是分层正确。

## 七、概念 → 课程落点对照

| 概念 | 代码落点 | 课程 |
|------|---------|------|
| 帧内分片 / 帧边界 | depacketizer 输出 `is_first_packet_in_frame`、`frame_type`（kVideoFrameKey=I / kVideoFrameDelta=P） | **5.1** |
| 同帧聚合 / seq 缺口检测 | **PacketBuffer**：按 timestamp 分组、按 seq 排序、检测缺包决定帧完整性 | **5.3** |
| 完整帧对象 | **RtpFrameObject**：timestamp + 首尾 seq + codec + video_header | **5.4** |
| 组帧 → 回调 | PacketBuffer 把完整帧交出去 → `OnFrame` → PC（SFU 只打日志验证链路） | **5.5** |
| 丢包重传 | `NackRequester`（seq 缺口 → RTCP NACK → RTX） | **6.x** |
| 丢帧自救 | 解码失败/帧不完整 → 请求关键帧 → `BuildPli` | **8.2~8.3** |

## 八、一句话记忆

> **一帧 = 一个 AU（解码输出一幅画面的 NAL 集合），被切成多个 RTP 包，靠 timestamp 认亲、靠 seq 排队、靠 M 位收尾；丢一包坏一帧（帧内），断一帧废一串（帧间参考链）——NACK 是包级自救，PLI 是帧级重建，5.x 的组帧主要为 PLI 提供判断燃料。**
