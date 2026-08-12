# v2_4 SR 包格式详解

> 完成时间：2026-08-12（4.12 提交后、4.13 移植前讨论定调）
> 定位：SR 是 RTCP 家族里"发送端的报告"——推流端周期发给收流端，汇报"我发了多少"。它是 4.12 LSR/DLSR 的源头（`remote_sr` 就来自 SR 的 NTP 时间戳），也是 4.13 `HandleSr` 解析的对象。
> 配套：v2_4-RR包格式详解（RR 格式 + 报告块 7 字段）、v2_4.12（FeedbackState/LSR/DLSR）、v2_3_10（RTCP 公共头解析）

## 一、SR 包完整格式

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|    RC   |   PT=SR=200   |             length            |  ┐
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │ 头部
|                 SSRC of sender (= 媒体流 SSRC)                 │  │ 8 字节
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+  ┘
|          NTP timestamp, most significant word                 |  ┐
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │
|          NTP timestamp, least significant word                |  │
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │ sender info
|                      RTP timestamp                            │  │ 20 字节
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │
|                  sender's packet count                        │  │
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │
|                   sender's octet count                        │  ┘
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
|                 SSRC_1 (first source)                         |  ┐
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │ 报告块
| fraction lost |       cumulative number of packets lost       │  │ 24 字节
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │ × 0~31
|           extended highest sequence number received           │  │ (与 RR 完全相同)
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │
|                      interarrival jitter                      │  │
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │
|                         last SR (LSR)                         │  │
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+  │
|                   delay since last SR (DLSR)                  │  ┘
+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
```

**结构 = 8 字节头 + 20 字节 sender info + RC 个 24 字节报告块**。一个报告块时整包 52 字节。

## 二、头部字段：与 RR 完全相同的 5 个字段

| 字段 | 位数 | 含义 | 与 RR 差异 |
|------|:---:|------|---------|
| **V (Version)** | 2 | 版本号，恒为 2 | 无 |
| **P (Padding)** | 1 | 1 = 包尾有 padding | 无 |
| **RC (Reception Report Count)** | 5 | 报告块个数（0~31），**可为 0** | 无 |
| **PT** | 8 | **SR = 200**（RR = 201） | 仅 PT 不同 |
| **length** | 16 | 字长减 1：`length = 字节数/4 - 1` | 一个块时 = 52/4-1 = **12** |

## 三、重点：SSRC of sender = 媒体流 SSRC

SR 头部的 SSRC of sender **就是这路媒体流（视频或音频）的 SSRC**，不是"随便一个标识"。理由有三：

1. **RFC 3550 规定 RTP 和 RTCP 共享 SSRC 空间**（§5.1）：RTCP 包用媒体流的 SSRC 来声明"我属于这路流"。一路视频流一个 SSRC，SR 用它表明"我是在替这路流做发送报告"。
2. **sender info 的统计对象就是这路流**：NTP/RTP 时间戳、packet count、octet count 全部是"这路 RTP 流发了多少"。SSRC of sender 是这 20 字节统计的锚点——它既是"谁发的包"，也是"统计的是哪路流"。
3. **本工程实践**：`RTCPSender::ssrc_` 就是视频流 SSRC（与 RTP 包同一 SSRC）；4.13 的 `HandleSr` 用 `sr.sender_ssrc() == remote_ssrc_` 过滤，`remote_ssrc_` 就是远端视频流 SSRC（`remote_video_ssrc_`）——"只处理我关注的那路视频流的 SR"。

> 纯推流端发 SR 时报告块为空（RC=0）：它没在收流，无接收统计可报。sender info 照带。SFU 既是接收端又是发送端，它发 SR 给下游时报告块非空——报告块逻辑与 RR 共用一份（4.11/4.12 的 `CreateRtcpReportBlocks`）。

## 四、SR vs RR 的 SSRC of sender：相同点与区别

### 相同点

| 相同点 | 说明 |
|--------|------|
| **字段定义一致** | 都是头部第 4 个 32-bit 字，RFC 3550 上写着同一句"SSRC of sender"——发送这个 RTCP 包者的 SSRC |
| **实际都填媒体流 SSRC** | 收发双方的 SSRC 都来自媒体流。SR 填发送方自己的媒体流 SSRC；RR 填接收方自己的媒体流 SSRC（接收方若也在推流） |
| **都回答"我是谁"** | 不是"我在汇报谁"——被汇报的流在报告块的 SSRC_1 里 |

### 区别

| 区别点 | SR | RR |
|--------|----|----|
| **谁在填** | 推流端（发送方）填自己的媒体流 SSRC | 收流端（接收方）填自己的媒体流 SSRC |
| **角色分量** | **双职**：既是"发送者标识"，也是 sender info 20 字节统计的**锚点**（统计属于哪路流） | **单职**：RR 没有 sender info，sender SSRC 只是"谁发的这份报告" |
| **与报告块的关系** | 报告块汇报的是**另一条**流（发送方也在收的流）；SSRC of sender ≠ 报告块 SSRC_1 | 同样：SSRC of sender（我）≠ 报告块 SSRC_1（你） |
| **本工程的用途** | 4.13 `HandleSr` 拿它匹配 `remote_ssrc_`，判断"是否我关注的流的 SR" | 解析 RR 时同样可用于区分报告归属 |

一句话总结：**两个包头部的 SSRC of sender 字段含义相同（都是发送者的媒体流 SSRC），但 SR 里它还多兼了一个"发送统计锚点"的职责**——因为 SR 是唯一自带"发送了多少"统计的 RTCP 包。

## 五、sender info 6 个字段（20 字节）

| 字段 | 位数 | 含义 | 代码对照 |
|------|:---:|------|---------|
| **NTP timestamp** | 64 | 发 SR 时刻的 NTP 时间（秒 + 分数，1900 纪元）。**接收端收到后原样抄进自己 RR 报告块的 LSR** | 4.12 `remote_sr` 的来源 |
| **RTP timestamp** | 32 | 与 NTP 同刻的 RTP 时间戳（视频 90kHz） | `SenderReport::rtp_timestamp` |
| **sender's packet count** | 32 | 累计已发 RTP 包数 | `SenderReport::sender_packet_count` |
| **sender's octet count** | 32 | 累计已发 RTP **载荷**字节数（不含 RTP 头） | `SenderReport::sender_octet_count` |

对应本工程 `SenderReport` 类（rtcbase）：

```cpp
static constexpr uint8_t kPacketType = 200;   // PT = SR
NtpTime ntp_;                                  // ← remote_sr 的来源
uint32_t rtp_timestamp_;
uint32_t sender_packet_count_;
uint32_t sender_octet_count_;
std::vector<ReportBlock> report_blocks_;       // 与 RR 共用同一结构
```

## 六、报告块：与 RR 完全相同

SR 的报告块**不是**把收到的 RR 抄回去，而是本端**独立实时计算**的接收统计（同一个数据源 `receive_stat_->RtcpReportBlocks()`）。报告块永远表达"**我**看你那路流的接收情况"，与包类型无关。

唯一"原样抄"的字段是 **LSR**：把对方 SR 里的 NTP 时间戳复制进自己的报告块（4.12 `remote_sr`），其他 5 个字段（fraction lost、累计丢包、序号、jitter、DLSR）全是本端自己算的。

## 七、数据流与课程对应

```
推流端周期发 SR（PT=200, sender info + 可选报告块）
  → 收流端 DeliverRtcp → RtpRtcpImpl::IncomingRtcpPacket (4.13)
  → RTCPReceiver::ParseCompoundPacket 拆包 (4.10 框架)
  → case SenderReport::kPacketType: HandleSr (4.13)
  → sr.sender_ssrc() == remote_ssrc_ 过滤 (4.13)
  → 提取 NTP → last_rr_ntp_secs/frac + remote_sr (4.14 填 FeedbackState)
  → 自己的 RR 报告块 LSR/DLSR 填充 (4.12 已就位, 等数据)
```

## 八、课程差异记录

- SR 与 RR 的唯一结构差异：报告块前多 20 字节 sender info；PT 200 vs 201
- RC 可为 0：纯推流端 SR 无报告块，合法
- 一个报告块时：SR 整包 52 字节，`length = 12`（52/4-1）
- 4.13 只加 ssrc 过滤 + `SetRemoteSsrc`，SR 解析本体（`sr.Parse` + 打日志）4.12 参考里就有
