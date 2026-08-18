# 从 1V1 推导到 1V多：SFU 截断、NACK 透传污染与 SSRC/Seq 冲突

> 7.1 理论课。三个核心问题：
> ① 1V1 → 1V多 的架构推导与详细架构
> ② 为什么 SFU 必须**截断**下行反馈（具备 RTCP 解析 + 构造能力），而不是透传
> ③ 什么时候需要考虑 SSRC / Sequence Number 冲突，当前 xrtcserver2.0 需不需要
>
> 本文从当前工程的实际代码出发（`rtc_stream_manager.cpp` 的透传现状），推导到 1V多 需要的改造。

---

## 一、起点：1V1 透传的完整架构

### 1.1 拓扑与转发路径

```
① RTP 路径（单向右行，原样转发，不改 SSRC/seq）

   推流端 ──RTP/SRTP 上行──► [PushStream] ──SendRtp──► [PullStream] ──RTP/SRTP 下行──► 拉流端
                             (1 个 PC，收上行)        (1 个 PC，发下行)

② RTCP 路径（双向原样透传 —— 当前 1V1 的实现）

   上行：推流端 SR ──SendRtcp 原样透传──► 拉流端         ← 推流端是媒体发送者 → 发 SR
   下行：拉流端 RR/NACK/PLI ──SendRtcp 原样透传──► 推流端   ← 拉流端是媒体接收者 → 发 RR

   ⇨ 媒体单向右，RTCP 双向绕回 = "透传"的形态：SFU 只是转发器，反馈的终点在两端，不在 SFU。
```

> **SR/RR 判定（RFC 3550）**：按"这个报告周期内我发过 RTP 没有"二选一——发过 → SR，没发过 → RR。
> 推流端是纯发送者 → **SR**；拉流端是接收者 → **RR**（+ NACK/PLI）。当前代码印证：`rtcp_receiver` 的 `HandleSr` 解析的就是推流端发来的 SR（取 NTP 算 RTT/LSR），`rtcp_sender` 只有 `BuildRR`（SFU 作为接收者向推流端报接收统计），**全工程尚无 `BuildSr`**——下行 SR 构造是 9.x 的活。

当前代码（`src/stream/rtc_stream_manager.cpp:222-247`）：

```cpp
// RTP 转发：push 收到 → 透传给 pull（原样，不改 SSRC/seq）
void RtcStreamManager::OnRtpPacketReceived(RtcStream* stream, const char* data, size_t len) {
    if (RtcStreamType::kPush == stream->stream_type()) {
        PullStream* pull_stream = FindPullStream(stream->get_stream_name());
        if (pull_stream) pull_stream->SendRtp(data, len);      // 原样转发
    }
}

// RTCP 转发：双向透传！
void RtcStreamManager::OnRtcpPacketReceived(RtcStream* stream, const char* data, size_t len) {
    if (RtcStreamType::kPush == stream->stream_type()) {
        PullStream* pull_stream = FindPullStream(stream->get_stream_name());
        if (pull_stream) pull_stream->SendRtcp(data, len);      // 上行 RTCP → 透传给拉流端
    } else if (RtcStreamType::kPull == stream->stream_type()) {
        PushStream* push_stream = FindPushStream(stream->get_stream_name());
        if (push_stream) push_stream->SendRtcp(data, len);      // 下行 RTCP（含拉流端 NACK）→ 透传给推流端
    }
}
```

`RtcStream::SendRtp / SendRtcp`（`src/stream/rtc_stream.cpp:107-117`）就是 `pc->SendRtp(data, len)` 原样交给 PC 的 SRTP session 发出去，**不解析、不重写**。拉流端 SDP 里协商的 SSRC 也是推流端的 SSRC（`PushStream::GetVideoSource` → `PullStream::AddVideoSource` 透传）。

### 1.2 1V1 下为什么 RTCP 可以透传

关键前提：**反馈的"归属"是唯一的**。

1V1 只有一条端到端媒体链路，链路只有一个接收端（拉流端）。因此：

| 反馈 | 描述的是 | 透传上去语义是否成立 |
|------|---------|:---:|
| 拉流端 NACK(103) | "整条链路里 103 我没收到" | ✅ 成立——就是推流端发出的 103 |
| 拉流端 RR | "整条链路的接收质量" | ✅ 成立——就是推流端→拉流端这一段 |
| 拉流端 PLI | "我要一个新的关键帧" | ✅ 成立——推流端编码器出关键帧即可 |

透传 = 反馈的**终点就是推流端**，SFU 只是中间转发器。客户端原生 libwebrtc 的整个 QoS 闭环（NACK→RTX、RR→GCC 拥塞控制、PLI→关键帧）**端到端生效**，SFU 不需要任何智能。

### 1.3 1V1 的隐含假设（推导的原料）

1V1 能透传，靠的是三个隐含假设，全部"想当然成立"：

- **H1：一条端到端链路** → 反馈归属唯一，无歧义。
- **H2：接收端唯一** → 反馈不需要"定位到人"，谁收到就是谁。
- **H3：恢复动作影响面 = 单个接收端** → RTX 重发只会被唯一那个接收端看到，不会打扰别人。

---

## 二、推导：逐条打破 1V1 的隐含假设

### 2.1 打破 H1/H2：N 条下行链路，反馈归属破裂

1V多 拓扑：1 个 push PC + N 个 pull PC（每个拉流端独立 PeerConnection / 独立 SRTP session）。

```
         推流端
           │
           │ 上行 RTP（同一份包）
           ▼
         SFU ──────► 拉流端A（下行链路A）
           │         │
           ├────────► 拉流端B（下行链路B，网络差）
           │         │
           └────────► 拉流端C（下行链路C）
```

- 上行链路：**一份** RTP，被复制广播给 A/B/C。
- 下行链路：**N 条独立** 的 RTP/SRTP session，各自独立丢包。

现在拉流端 B 的下行丢了 103：

```
B 实际收到：100 101 102  104 105   ← 缺 103
```

B 的 NACK(103) 描述的是**"SFU→B 这条下行链路的接收状态"**，**不**等价于"推流端→SFU 的上行丢了 103"（设计文档《1→N SFU 中的 NACK/RTX/RTP Cache》已论证）。

H1 被打破：推流端收到 NACK(103) 时**不知道是 A/B/C 谁的下行丢的**，更不知道"该单独救 B 还是广播"。

### 2.2 打破 H3：恢复动作影响面从"一个接收端"变成"所有接收端"

1V1 时推流端 RTX 重发 103 → 只有唯一那个接收端看到 → 无害。
1V多 时推流端 RTX 重发 103 → SFU 广播 → **A/B/C 全都收到这份 RTX**。B 需要它，A/C 不需要——这就是"污染"的根源（详见第四章）。

### 2.3 结论：SFU 必须从"中间转发器"升级为"反馈边界"

两个半环，各自闭环：

```
① 上行半环 —— SFU 当"接收端"，向推流端要包（6.x 已建完）

   推流端 ──RTP──► SFU
     ▲             │
     └── RTX 重传 ──┘    SFU 检测到上行丢包 → 发 NACK → 推流端 RTX 补包

② 下行半环 —— SFU 当"发送端 ×N"，用本地 Cache 给拉流端补包（7.x 要建）

   SFU ──RTP──► 拉流端B
     ▲             │
     └─ 本地重发 ───┘     B 检测到下行丢包 → 发 NACK → SFU 查 Cache → 就地重发，只给 B
```

- **上行半环** = 你 6.x 已经建完的机器：`NackRequester`（缺失记账/缺口触发/RTT 退避）+ `RtxReceiveStream`（RTX 解析/包恢复）+ `receive_stat`（接收统计）。
- **下行半环** = 7.x 之后要补：下行 RTP Cache + 拉流端 NACK 的截断处理。

这就是 1V多 的本质：**SFU 把一条端到端链路拆成两段各自闭环的链路，把客户端 libwebrtc 的接收端职责和发送端职责都"搬"到自己身上**。1V1 可以继承客户端全部 QoS，1V多 必须自己重做。

---

## 三、为什么 SFU 必须截断（RTCP 解析 + 构造能力）

### 3.1 "截断"的本质

截断不是"丢弃"，而是**把下行反馈的终点从推流端挪到 SFU 自己**：

```
拉流端 NACK(103) ──► SFU ──► (×) 不透传给推流端
                       │
                       └──► 查本地 Cache ── HIT → 就地重发 103，只给 B
                                       └── MISS → SFU 自己决定是否向推流端请求
```

要做到这一点，SFU 必须具备**读懂 RTCP** 和 **自己造 RTCP** 的能力。这是截断的前提，也是和"纯转发器"的本质区别。

### 3.2 SFU 截断需要的四种能力（与当前代码对照）

| 能力 | 用途 | 当前代码 | 状态 |
|------|------|---------|:---:|
| **解析上行 RTCP**（推流端 SR） | 维护上行接收统计（丢包/jitter/RTT），喂给 RR 上报 | `rtcp_receiver.h` `HandleSr` | ✅ 已有（4.x） |
| **构造上行 NACK** | 上行丢包时向推流端请求重传 | `rtcp_sender.h` `BuildNack` + `rtp_rtcp_impl.h` `SendNack` | ✅ 已有（6.x） |
| **解析下行 RTCP**（拉流端 NACK/RR/PLI） | 读懂每个拉流端的下行反馈，做归属定位 | 无 `HandleNack` | ⬅️ 7.x 新增 |
| **构造下行响应**（Cache 命中就地重发 / 对推流端的合并 NACK） | 截断后的就地恢复 | 下行 RTP Cache 未建 | ⬅️ 7.x 新增 |

注意：解析下行 NACK（`HandleNack`）是**7.x 的增量能力**——现在 `rtcp_receiver` 只解析 SR/RR（推流端发 SR、拉流端发 RR），还不会解析 NACK，因为上行方向推流端不会给 SFU 发 NACK（SFU 是上行请求方，不是响应方）。下行方向角色互换，SFU 变成 NACK 的接收方。

### 3.3 截断后的反馈路由表（1V多 设计目标）

| 反馈 | 来源 | SFU 处理 | 去向 |
|------|------|---------|------|
| 上行 SR | 推流端（媒体发送者） | 解析喂给 `receive_stat` | 统计用（4.x 已做），不必透传 |
| 上行 RTX | 推流端 | `RtxReceiveStream` 还原成原始 RTP → 广播 | 恢复后广播所有人（6.x 已做） |
| **下行 NACK** | 拉流端 | **截断**：Cache HIT 就地重发；MISS 合并后向推流端请求 | 只发给请求的拉流端 |
| 下行 RR | 拉流端 | 截断：解析喂给**下行发送统计**（9.x 做 SR 上报） | 统计用 |
| **下行 PLI** | 拉流端 | **合并节流**：多路 PLI 合成一上路请求（8.x `is_pli` + `BuildPli`） | 一次转发给推流端 |

PLI 和 NACK 的处理逻辑不同（这是 8.x 的单独一章）：

| | NACK | PLI |
|---|---|---|
| 恢复对象 | 历史 RTP 包 | 未来关键帧 |
| 影响面 | 单个拉流端 → 单播式，定位到人 | 所有拉流端 → 广播式，必须合并节流 |
| 透传后果 | 冗余 RTX 污染（下章） | 推流端频繁出关键帧 → 码率暴涨画质崩 |

---

## 四、1V多 下 NACK 透传会发生什么

### 4.1 场景推演：B 下行丢 103

推流端发出 100~105，SFU 广播。B 下行丢了 103，A/C 正常：

```
推流端
  │ 100 101 102 103 104 105
  ▼
 SFU ──► A：100 101 102 103 104 105  ✅ 全到
  │ ──► B：100 101 102    104 105     ← 缺 103
  │ ──► C：100 101 102 103 104 105  ✅ 全到
  ▲
  │ NACK(103) ← B 发给 SFU
```

### 4.2 透传的灾难链（当前代码就是透传！）

```
① B 下行丢 103 → B 发 NACK(103)
② SFU OnRtcpPacketReceived：pull 的 RTCP → push_stream->SendRtcp → 透传给推流端
③ 推流端收到 NACK(103) → 重传 103（RTX 或原包）
④ SFU OnRtpPacketReceived：收到重传的 103 → 广播给 A/B/C
   └── A 收到第 2 份 103（冗余！）  C 收到第 2 份 103（冗余！）
```

如果 C 的下行也丢 103（比如 C 也差）：

```
B → NACK(103)
C → NACK(103)     ← 两个 NACK 都透传上去
推流端收到两个 NACK(103) → 重传 2 次 → 广播 2 次
   └── A 收到 3 份 103（原包 + 2 份重传）
```

这还不是最坏：**RTX 放大**。N 个拉流端各有各的下行丢包，各自的 NACK 全部透传 → 推流端重传 N 份 → 广播 N 遍 → 每个拉流端都白收 N-1 份冗余。

### 4.3 污染拉流端的模块清单

拉流端是 VS2022 C++ 客户端（原生 libwebrtc），其接收管线正是本项目 6.x 复刻的那套结构（`receive_stat` / `nack_requester` / `histogram` / `rtx_receive_stream` / `rtp_video_stream_receiver`）。冗余 RTX 到达时，逐个模块看：

| 模块 | 收到冗余 RTX 后 | 后果 |
|------|---------------|------|
| **RtxReceiveStream** | 解析 RTX 信封 → 还原原始 seq=103 → 发现 103 已有 | 白做一轮解析+还原+查重；CPU 浪费（`src/video/rtx_receive_stream.cpp`） |
| **RtpVideoStreamReceiver / PacketBuffer** | 查重：103 已在队列 → 丢弃 | 去重正确则到此为止；去重不完美则重复包漏向下游 |
| **receive_stat（接收统计）** | RTX SSRC 是独立接收流，有自己的 seq 统计；若重复包在统计后再去重 | 丢包/乱序统计失真 → RR 报告错误 → 发送端（推流端/SFU）拥塞控制误判码率 |
| **histogram（乱序直方图）** | 还原出的 103 与已有 103 构成"重序" | 乱序率虚高 → jitter 计算失真（`src/modules/video_coding/histogram.cpp`） |
| **NackRequester 账本** | 已收的 seq 又被标记到达 | 缺口判断被扰动 → 误抑制/误发 NACK |
| **组帧/解码器** | 去重失败时，重复帧进解码器 | 参考帧错乱 → 花屏/卡顿 |
| **带宽** | 每端白收 N-1 份冗余 RTX | 下行带宽浪费，尤其是 N 大时 |

**结论**：即使 libwebrtc 去重完美，污染 = **带宽浪费 + CPU 浪费 + 统计失真（RR/GCC/jitter 误判）**；去重不完美则直接漏进解码器。核心伤害是把"B 一个人的下行问题"变成"所有人承担的成本"。

### 4.4 系统性污染：反馈放大与拥塞螺旋

最严重的是**恶性循环**，而不是单包冗余：

```
B 的下行拥塞/丢包 ↑
    → B 发更多 NACK
    → SFU 透传 → 推流端上行流量被 RTX 撑爆
    → 推流端上行也拥塞 → 上行丢包增多
    → 上行 NACK 增多 → 更多 RTX
    → 广播冗余更多 → A/C 下行也受影响
```

一个坏拉流端，通过透传把上行和其他拉流端**全部拖下水**。这就是设计文档里的核心原则：**一个 Subscriber 的坏网络应该局部化，不能反向污染 Publisher 和其他 Subscriber**。SFU 截断 + 本地 Cache 正是局部化的手段。

---

## 五、SSRC 与 Sequence Number 冲突

### 5.1 判定框架：冲突的边界 = SRTP session 的边界

SRTP session 的判定：**1 个 SRTP session = 1 次 DTLS 导出的密钥 + 1 个对端 + 1 个 role**。一个 session 内的所有流共享加密上下文，**SSRC 必须全局唯一**，否则解密/路由无法区分。

因此"要不要处理 SSRC 冲突"，取决于**一个 session 里塞了多少个媒体源**：

| 形态 | session 数 | SSRC 冲突 | 需要处理吗 |
|------|:---:|:---:|:---:|
| 1 用户 = 1 PC（多 m-line 汇聚一路） | 1 | 必然冲突 | **必须重写 SSRC** |
| 1 用户 = N PC（每路独立 PC/session） | N | 跨 session 撞 SSRC 无害 | **不需要** |

### 5.2 SSRC 冲突的具体触发场景

| 场景 | 说明 | 需要的处理 |
|------|------|-----------|
| **单 PC 多流（mediasoup 型 SFU）** | 多个上游流挤进拉流端一个 session | SFU 为每路分配唯一下行 SSRC + 同步重写 RTX 映射、RTCP SR/RR/PLI 里的 ssrc |
| **多 PC 形态（当前架构）** | 每路独立 session，跨路撞 SSRC 无感 | 无需处理 |
| **Simulcast** | 推流端每层一个 SSRC（多清晰度） | SFU 管理多 SSRC + 每层独立 RTX（FID 分组），按需订阅选层 |
| **下行 RTX 信封** | SFU 给拉流端重发时用 RTX 格式 | 需在拉流端 SDP 里协商 RTX SSRC + `a=ssrc-group:FID` |

### 5.3 Sequence Number 冲突的场景

seq 是 **SSRC 相对**的序号（16 bit 回绕），不同 SSRC 的 seq 可以重叠而无冲突。seq 冲突只有两种情况：

| 场景 | 说明 | 需要的处理 |
|------|------|-----------|
| **下行重写 seq** | SFU 给每个拉流端独立的下行 seq 空间时 | 拉流端 NACK 里是下行 seq，必须做 `下行seq → 上行seq` 映射（sequence-space mapping） |
| **同一 SSRC 被复用** | 推流端换流/异常时复用了 SSRC | 接收侧 seq 回绕/突变检测（`receive_stat` 4.7 已做） |

**关键认识**：如果你**不重写**下行 SSRC 和 seq（透传），拉流端 NACK 里的 (SSRC, seq) 天然就是推流端的 (SSRC, seq)，**SFU 直接用它当 Cache 的 key，零映射成本**。

### 5.4 当前 xrtcserver2.0 需要处理吗？——不需要

理由，逐条对照：

```
① 每个拉流端 = 独立 PeerConnection = 独立 SRTP session
   → 跨拉流端撞 SSRC 无感，无需重写          （多 PC 形态）
② 下行 SSRC 透传（PullStream::AddVideoSource 用推流端 SSRC）
   → 拉流端看到的 SSRC 就是推流端的          （无映射）
③ 下行 seq 透传
   → 拉流端 NACK 的 seq 就是 Cache 的 key    （无映射）
④ RTX 上行恢复后，广播的是**还原后的原始 RTP**（6.6 recovered()）
   → 广播出去的包保持原始 SSRC/seq           （无需重写）
```

所以当前架构**不需要** RTP sequence-space mapping。这也是设计文档的结论：独立 PC 提供了天然的 session 隔离，SSRC/seq 透传让 Cache 查找零成本。

### 5.5 将来触发条件（什么时候才需要回头处理）

| 触发点 | 说明 |
|--------|------|
| 做**单 PC 多流会议**（一个拉流端一个 PC 拉多路） | 多路挤一个 session → 必须重写下行 SSRC |
| 做 **Simulcast**（多清晰度层） | 多 SSRC + 每层 RTX 管理，下行按层分配 |
| 做**下行 RTX 信封**（Cache HIT 用 RTX 格式重发） | 拉流端 SDP 需协商 RTX SSRC + FID 分组 |
| 重写任何下行 RTP 头（SSRC/seq/MID/RID/TWCC） | 一旦重写，就要引入 seq mapping 回查上行 |

一句话：**只要保持"多 PC + SSRC/seq 透传"，冲突就不会发生；冲突处理是"重写下行空间"才欠下的债**。

---

## 六、总结

```
1V1 能透传    = 反馈归属唯一（一条链路、一个接收端）→ 客户端 QoS 全闭环
1V多 必须截断  = 反馈归属破裂（N 条下行链路）→ SFU 把端到端拆成两段各自闭环
截断的前提    = SFU 具备 RTCP 解析能力（读懂拉流端 NACK）+ 构造能力（就地重发/上行请求）
不透传的后果  = 冗余 RTX 污染所有拉流端（带宽/CPU/统计失真 + 反馈放大拥塞螺旋）
SSRC/Seq 冲突 = 边界是 SRTP session；多 PC + 透传 → 不需要；重写下行空间才欠债
```

> 落地到课程：7.3 双模式（transparent 保留 1V1 透传 / live 走截断）→ 7.4 `multi_pull_streams_`（stream→uid→PullStream）→ 7.5 生命周期 → 7.6 按 mode 分流 → 7.7 上行 RTP 上报链 → 7.8 一推多拉广播。下行 NACK 截断 + Cache 在 7.x 结构打通后接入，PLI 合并节流在 8.x，每路下行发送统计 + SR 在 9.x。
