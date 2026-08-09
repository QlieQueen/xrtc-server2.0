# 1→N SFU 中的 NACK / RTX / RTP Cache 设计详解

> 场景：一个 Publisher 推流端，经由 SFU 分发给多个 Subscriber 拉流端。每个 Subscriber 使用独立 PeerConnection / RTP Session。
>
> 本文总结 1→N SFU 中 NACK、RTX、RTP Cache、PeerConnection 隔离、RTP sequence-space mapping，以及 Cache Miss 后向 Publisher 请求重传之间的关系。

---

## 1. 问题背景

一个典型的 SFU 拓扑：

```text
                         Publisher
                             │
                             │ RTP
                             ▼
                            SFU
                  ┌──────────┼──────────┐
                  │          │          │
                  ▼          ▼          ▼
                PC-A       PC-B       PC-C
                  │          │          │
                  ▼          ▼          ▼
              Subscriber A Subscriber B Subscriber C
```

假设：

- Publisher 的网络正常；
- Subscriber A 网络正常；
- Subscriber B 网络很差；
- Subscriber C 网络正常。

Publisher 发出：

```text
RTP Seq:
100 101 102 103 104 105
```

SFU 将 RTP 分发给 A、B、C。

B 因为自己的下行链路丢失了 `Seq=103`：

```text
B 实际收到：

100 101 102    104 105
             ↑
            丢失
```

于是 B 向 SFU 发送：

```text
NACK(103)
```

这里必须首先建立一个非常重要的认识：

> **B 的 NACK 描述的是 B 所看到的 RTP 接收状态，也就是 SFU→B 这条下行链路上的丢包情况。**

它并不天然意味着：

> Publisher→SFU 发生了丢包。

---

# 2. Subscriber 的 NACK 到底在描述什么？

可以把 NACK 的产生过程理解为：

```text
Publisher
    │
    │ RTP
    ▼
   SFU
    │
    │ RTP
    ▼
Subscriber B
    │
    │ 接收 RTP
    │
    ├── 100
    ├── 101
    ├── 102
    ├── 104
    └── 105
          ↑
       103 丢失
```

B 的 RTP Receiver State 发现：

```text
expected = 103
actual   = 104
```

于是产生：

```text
NACK(103)
```

所以 NACK 的语义更接近：

```text
“在我的 RTP 接收会话中，Seq=103 没有到达。”
```

而不是：

```text
“Publisher 的 RTP Seq=103 在整个系统里丢了。”
```

这是理解 1→N SFU RTCP feedback 的核心。

---

# 3. 为什么 1→1 时“简单透传 NACK”看起来没问题？

如果只有：

```text
Publisher
    │
    ▼
   SFU
    │
    ▼
Subscriber
```

那么：

```text
Subscriber
    │
    │ NACK(103)
    ▼
   SFU
    │
    │ NACK(103)
    ▼
Publisher
```

这个时候问题不大，因为整个系统只有一个 Subscriber。

Publisher 收到：

```text
NACK(103)
```

它基本可以理解为：

> “我发出的 Seq=103，接收端没有收到，请重新发送。”

即使 SFU 自己不维护 RTP Cache，很多情况下也可以直接让 Publisher 处理 NACK。

---

# 4. 1→N 后为什么发生本质变化？

现在变成：

```text
                         Publisher
                             │
                             ▼
                            SFU
                    ┌────────┼────────┐
                    │        │        │
                    ▼        ▼        ▼
                    A        B        C
                  网络好    网络差    网络好
```

B：

```text
B → SFU：NACK(103)
```

C：

```text
C → SFU：没有 NACK
```

如果 SFU 直接：

```text
B NACK(103)
    │
    ▼
   SFU
    │
    ▼
Publisher
```

Publisher 看到的只有：

```text
NACK(103)
```

Publisher 并不知道：

```text
这是 B 发的
```

更不知道：

```text
B 的网络非常差
A 的网络非常好
C 的网络非常好
```

因此，Publisher 无法根据这个 NACK 判断：

> “只需要针对 B 做恢复。”

它看到的只是：

> “有人请求 Seq=103。”

---

# 5. 多个 Subscriber 的 NACK 会产生反馈汇聚

假设：

```text
A：网络好
B：网络差
C：网络差
```

可能出现：

```text
B → SFU：NACK(103)
C → SFU：NACK(103)
```

如果 SFU 透明透传：

```text
SFU → Publisher：

NACK(103)
NACK(103)
```

甚至：

```text
NACK(103)
NACK(103)
NACK(105)
NACK(106)
NACK(105)
...
```

Publisher 可能因此产生大量 retransmission。

这就是：

> **NACK amplification / retransmission amplification**

---

# 6. RTX amplification

假设 Publisher 收到：

```text
NACK(103)
NACK(103)
NACK(105)
```

Publisher 可能产生：

```text
RTX(103)
RTX(103)
RTX(105)
```

如果 SFU 还是简单透明转发：

```text
Publisher
    │
    │ RTX
    ▼
   SFU
  /   \
 ▼     ▼
 A     B
```

那么 A 明明网络很好：

```text
A 已经收到 103
```

却可能再次收到：

```text
RTX(103)
```

这就是问题。

---

# 7. 真正的核心：NACK 有“归属”

NACK 不能只看：

```text
SSRC
Sequence Number
```

还需要知道：

```text
这个 RTCP feedback 来自哪个 RTP Session？
```

例如：

```text
PC-A
 └── RTP Session A
      └── NACK(SSRC=111, Seq=103)

PC-B
 └── RTP Session B
      └── NACK(SSRC=111, Seq=103)
```

虽然两个 NACK 的：

```text
SSRC = 111
Seq  = 103
```

完全相同，

但它们的含义不同：

```text
A：我 A 没收到 103
B：我 B 没收到 103
```

所以：

> **NACK 的 RTP 标识 + NACK 所属的接收会话上下文，才构成完整的反馈归属。**

---

# 8. 独立 PeerConnection 为什么天然解决了这个问题？

如果 SFU 架构是：

```text
SFU
 ├── PeerConnection A
 │      └── Subscriber A
 │
 ├── PeerConnection B
 │      └── Subscriber B
 │
 └── PeerConnection C
        └── Subscriber C
```

那么：

```text
PC-A → A
PC-B → B
PC-C → C
```

每个 PeerConnection 都有自己的：

```text
RTP
RTCP
SSRC
Sequence Number
NACK
RR
TWCC
RTX
```

因此当 SFU 收到：

```text
NACK(SSRC=111, Seq=103)
```

它不需要仅仅通过 SSRC 猜测：

```text
“到底是谁的？”
```

因为它已经知道：

```text
这个 RTCP packet 是从 PC-B 到达的
```

所以：

```text
NACK(111,103) + source PC-B
```

等价于：

```text
B 请求恢复 Publisher RTP 111:103
```

---

# 9. RTP Sequence-Space Mapping 到底解决什么？

假设 SFU 为每个 Subscriber 建立独立的 RTP space。

Publisher：

```text
SSRC=111
Seq=103
```

经过 SFU 后：

```text
Subscriber B：

SSRC=2001
Seq=803
```

那么：

```text
B
 │
 │ NACK(2001,803)
 ▼
SFU
```

SFU 必须做：

```text
(2001,803)
      │
      ▼
Publisher
(111,103)
```

这就是：

> RTP sequence-space mapping。

---

# 10. 但 RTP Space Mapping 不是绝对必须

还有一种实现：

```text
Publisher RTP
SSRC=111
Seq=103
```

SFU 不修改：

```text
SSRC=111
Seq=103
```

直接给 A/B/C 使用。

然后：

```text
PC-A
 └── Subscriber A

PC-B
 └── Subscriber B

PC-C
 └── Subscriber C
```

B：

```text
PC-B → SFU
NACK(111,103)
```

SFU 已经知道：

```text
这个 NACK 属于 B
```

因此可以：

```text
NACK(111,103)
      │
      ├── RTP identifier：找到哪个 RTP
      │
      └── PC-B：找到应该给谁恢复
```

所以：

> **不做 Publisher Seq → Subscriber Seq 的数值 mapping，也可以实现正确的 per-subscriber retransmission。**

前提是：

1. SFU 能定位原始 RTP；
2. SFU 能知道 NACK 属于哪个 Subscriber；
3. SFU 能只把恢复包发送给该 Subscriber。

---

# 11. RTP Cache 的作用

SFU 可以维护：

```text
RTP Cache
```

例如：

```text
Cache key：

(SSRC, Sequence Number)
```

内容：

```text
111:100 → RTP Packet
111:101 → RTP Packet
111:102 → RTP Packet
111:103 → RTP Packet
111:104 → RTP Packet
```

于是：

```text
Subscriber B
     │
     │ NACK(111,103)
     ▼
    SFU
     │
     ▼
Cache[111][103]
     │
     ▼
   HIT
```

SFU 不需要通知 Publisher。

直接：

```text
SFU
 │
 │ retransmission
 ▼
PC-B
 │
 ▼
Subscriber B
```

---

# 12. Cache Hit：最理想的恢复路径

完整流程：

```text
B
│
│ NACK(103)
▼
SFU
│
│ 查 RTP Cache
▼
Cache HIT
│
│ 找到 RTP 103
▼
SFU
│
│ RTX / retransmission
▼
PC-B
│
▼
B
```

Publisher：

```text
完全不知道
```

A：

```text
完全不知道
```

C：

```text
完全不知道
```

这就是非常理想的隔离。

---

# 13. 为什么 SFU Cache 不需要永久保存？

RTP Cache 通常只需要保存一个滑动窗口：

```text
最近 N 个 RTP packet
```

或者：

```text
最近几百毫秒 / 几秒
```

因为 NACK 通常针对近期丢包。

例如：

```text
当前 Seq = 10000

Cache：

9990
9991
...
10000
```

如果 B NACK：

```text
9998
```

很可能：

```text
Cache HIT
```

如果 B 很久以后才 NACK：

```text
5000
```

那么：

```text
Cache MISS
```

此时进入第二级恢复。

---

# 14. Cache Miss：向 Publisher 请求恢复

假设：

```text
Subscriber B
NACK(5000)
```

SFU：

```text
Cache[5000]
    ↓
MISS
```

那么可以：

```text
SFU
 │
 │ NACK(5000)
 ▼
Publisher
```

这里 SFU 的意思是：

> “我本地已经没有这个 RTP 包了，请你看看你的 retransmission history 里是否还有。”

Publisher 的 WebRTC RTP sender 通常可能还有自己的 retransmission history。

于是：

```text
Publisher
    │
    │ RTX
    ▼
   SFU
    │
    │ 只给 B
    ▼
   PC-B
    │
    ▼
    B
```

---

# 15. 这里存在“两级 RTP Recovery”

因此可以把架构理解成：

```text
                 Subscriber
                     │
                    NACK
                     ▼
                    SFU
                     │
              ┌──────┴──────┐
              │             │
           Cache HIT     Cache MISS
              │             │
              ▼             ▼
         Local RTX       NACK
              │             │
              ▼             ▼
         Subscriber     Publisher
                              │
                             RTX
                              │
                              ▼
                             SFU
                              │
                              ▼
                         Subscriber
```

也就是：

### Level 1

```text
SFU RTP Cache
```

### Level 2

```text
Publisher Retransmission History
```

---

# 16. SFU 向 Publisher 发 NACK 时需要注意什么？

如果 SFU 没有修改 Publisher 的 RTP：

```text
Publisher:
SSRC=111
Seq=103
```

Subscriber B 也直接使用：

```text
SSRC=111
Seq=103
```

那么 B：

```text
NACK(111,103)
```

SFU Cache Miss 后可以比较直接地：

```text
SFU → Publisher
NACK(111,103)
```

但如果 SFU 已经建立了独立 RTP space：

```text
Publisher:
111:103

B:
2001:803
```

B：

```text
NACK(2001,803)
```

那么 SFU 必须先：

```text
2001:803
    ↓ mapping
111:103
```

再向 Publisher：

```text
NACK(111,103)
```

所以：

> **是否需要 RTP Space Mapping，最终取决于 SFU 是否修改/生成了独立的下行 RTP sequence space。**

---

# 17. 为什么不能把 RTX 当成普通 RTP 广播？

因为 RTX 本质上也是针对某个接收者的恢复行为。

理想情况：

```text
B 丢了 103

B
 │
 │ NACK
 ▼
SFU
 │
 │ RTX
 ▼
B
```

而不是：

```text
B 丢了 103

B
 │
 │ NACK
 ▼
SFU
 │
 ▼
Publisher
 │
 │ RTX
 ▼
SFU
 │
 ├──► A
 ├──► B
 └──► C
```

后者把：

```text
B 的网络问题
```

变成：

```text
A/B/C 所有人都要承担的额外流量
```

这违背了 SFU 一个很重要的设计原则：

> **一个 Subscriber 的坏网络应该尽可能局部化，不应该反向污染 Publisher 和其他 Subscriber。**

---

# 18. 一个网络差的 Subscriber 为什么可能把 Publisher 拖入拥塞？

假设 B 网络极差：

```text
B loss = 20%
```

如果 SFU 直接透传 NACK：

```text
B
 │
 │ NACK ↑↑↑
 ▼
SFU
 │
 │ NACK ↑↑↑
 ▼
Publisher
 │
 │ RTX ↑↑↑
 ▼
SFU
```

那么 Publisher→SFU 的流量增加。

如果 Publisher 上行链路已经接近容量：

```text
正常 RTP
+
RTX
```

可能进一步造成：

```text
拥塞
 ↓
丢包
 ↓
更多 NACK
 ↓
更多 RTX
```

形成：

```text
下行问题
   ↓
NACK
   ↓
Publisher RTX
   ↓
额外流量
   ↓
拥塞
   ↓
更多丢包
   ↓
更多 NACK
```

所以透明 NACK/RTX 在 1→N 下不仅仅是“多发几个包”的问题，而可能造成反馈与重传放大。

---

# 19. 但 NACK 不是用来解决持续严重拥塞的

NACK/RTX 更适合：

```text
短时间
少量
随机丢包
```

例如：

```text
100
101
102
    103 ← 偶尔丢一个
104
105
```

适合：

```text
NACK → RTX
```

但如果：

```text
loss = 20%
```

还持续：

```text
NACK
RTX
NACK
RTX
NACK
RTX
```

就不合理了。

持续严重丢包应该通过 WebRTC 的拥塞控制/码率适配机制降低发送负担，而不是无限增加 RTX。

---

# 20. Subscriber 的 PLI 应该如何处理？

如果 Subscriber 已经无法通过 NACK/RTX 恢复历史 RTP：

```text
NACK
 ↓
SFU Cache MISS
 ↓
Publisher RTX history MISS
```

这个历史 RTP 可能已经无法恢复。

对于视频，最终可能需要：

```text
Keyframe Recovery
```

例如：

```text
Subscriber
    │
    │ PLI
    ▼
   SFU
    │
    │ PLI
    ▼
Publisher
    │
    ▼
Encoder
    │
    ▼
Keyframe
```

对于基于原生 WebRTC C++ 的客户端：

> SFU 通常不需要自己实现“什么时候应该 PLI”的视频解码状态机。

接收端的 WebRTC RTP/视频接收链路会根据自身恢复和解码状态决定是否需要关键帧请求。

SFU 主要负责：

```text
Subscriber → SFU → Publisher
```

正确路由 PLI/FIR。

---

# 21. “RTX unavailable → 一定立刻 PLI”需要谨慎理解

不能简单写成：

```text
RTX unavailable
    ↓
必然立即 PLI
```

更准确的逻辑是：

```text
RTP packet lost
       │
       ▼
     NACK
       │
       ▼
    RTX recovery
       │
       ├── 成功 → 继续解码
       │
       └── 无法恢复
              │
              ▼
      接收端/解码状态判断
              │
              ▼
          PLI / FIR
              │
              ▼
          Keyframe
```

所以 SFU 不需要因为：

```text
Cache MISS
```

就自己强行生成 PLI。

Subscriber 自己产生 PLI 时，SFU 正确转发即可。

---

# 22. 推荐的第一版 SFU 架构

对于自己实现 SFU，尤其是：

```text
1 Publisher
N Subscriber
每个 Subscriber 一个 PeerConnection
```

可以采用：

```text
                         Publisher
                             │
                             │ RTP
                             ▼
                    ┌─────────────────┐
                    │       SFU       │
                    │                 │
                    │   RTP Cache     │
                    │                 │
                    └────────┬────────┘
                             │
             ┌───────────────┼───────────────┐
             │               │               │
             ▼               ▼               ▼
           PC-A            PC-B            PC-C
             │               │               │
             ▼               ▼               ▼
             A               B               C
```

核心状态：

```text
SFU
├── Publisher RTP state
├── RTP Cache
├── Subscriber A state
├── Subscriber B state
├── Subscriber C state
└── per-subscriber retransmission state
```

---

# 23. NACK 的推荐处理流程

```text
                Subscriber B
                     │
                     │ NACK
                     ▼
                    SFU
                     │
                     │ ① 判断 NACK 来源
                     │    = PC-B
                     │
                     │ ② 定位 RTP
                     │
                     ▼
                RTP Cache
                 /       \
              HIT         MISS
               │             │
               ▼             ▼
          Local RTX       NACK
               │             │
               │             ▼
               │         Publisher
               │             │
               │          RTX / miss
               │             │
               │             ▼
               └──────────► SFU
                              │
                              ▼
                            PC-B
                              │
                              ▼
                              B
```

最重要的一点：

```text
最终 retransmission 只发送给 B
```

而不是：

```text
A + B + C
```

---

# 24. PLI 的推荐处理流程

```text
Subscriber B
     │
     │ PLI
     ▼
    SFU
     │
     │ RTCP feedback routing
     ▼
Publisher
     │
     ▼
Encoder
     │
     ▼
Keyframe
     │
     ▼
SFU
     │
     ▼
Subscriber B / 按实际媒体分发策略发送
```

PLI 与 NACK 的角色不同：

| Feedback | 主要目的 |
|---|---|
| NACK | 请求恢复某些丢失的 RTP packet |
| RTX | 实际执行历史 RTP 的 retransmission |
| PLI | 请求新的关键帧 |
| FIR | 更强的关键帧请求，通常用于需要重新发送完整关键帧的场景 |

---

# 25. NACK / RTX / PLI 三者的关系

可以把它们理解成三个层级：

```text
        丢包
         │
         ▼
       NACK
         │
         ▼
    RTX / retransmission
         │
    ┌────┴────┐
    │         │
   成功      失败
    │         │
    ▼         ▼
 继续解码   Keyframe
              │
              ▼
          PLI / FIR
```

所以：

```text
NACK
```

是：

> “我缺少这个具体 RTP 包。”

而：

```text
PLI
```

是：

> “我已经不能可靠地依靠历史 RTP 恢复了，请给我一个新的关键帧重新建立解码状态。”

---

# 26. 最重要的架构原则

整个 1→N SFU 可以归纳成四条：

## 原则 1：Feedback 属于 Subscriber

```text
Subscriber NACK
```

首先应该理解成：

```text
该 Subscriber 的接收状态
```

而不是：

```text
Publisher 的全局状态
```

---

## 原则 2：坏 Subscriber 的问题应该局部化

理想：

```text
B loss
 ↓
B NACK
 ↓
SFU local recovery
 ↓
B RTX
```

避免：

```text
B loss
 ↓
B NACK
 ↓
Publisher
 ↓
RTX
 ↓
A/B/C
```

---

## 原则 3：优先本地 RTP Cache

恢复优先级：

```text
Subscriber
   ↓
SFU RTP Cache
   ↓
Cache HIT
   ↓
Local RTX
```

因为这样：

- 不需要打扰 Publisher；
- 不增加 Publisher→SFU 上行流量；
- 不会把一个 Subscriber 的问题传播到其他 Subscriber；
- RTT 更低；
- SFU 对 retransmission 有更强控制能力。

---

## 原则 4：Cache Miss 再向上游请求

```text
SFU Cache MISS
       ↓
NACK → Publisher
       ↓
Publisher RTX
       ↓
SFU
       ↓
目标 Subscriber
```

这是一个非常自然的二级恢复体系。

---

# 27. 一个完整的最终模型

可以把整个系统记成：

```text
                         Publisher
                             │
                             │ RTP
                             ▼
                    ┌─────────────────┐
                    │       SFU       │
                    │                 │
                    │   RTP Cache     │
                    │                 │
                    └────────┬────────┘
                             │
             ┌───────────────┼───────────────┐
             │               │               │
             ▼               ▼               ▼
            PC-A            PC-B            PC-C
             │               │               │
             A               B               C
                             │
                             │ NACK
                             ▼
                            SFU
                             │
                     ┌───────┴────────┐
                     │                │
                  Cache HIT        Cache MISS
                     │                │
                     ▼                ▼
                 RTX → B       NACK → Publisher
                                      │
                                      ▼
                                     RTX
                                      │
                                      ▼
                                     SFU
                                      │
                                      ▼
                                      B
```

如果 Publisher 也没有该 RTP：

```text
Subscriber
    │
    │ NACK
    ▼
   SFU
    │
    │ Cache MISS
    ▼
Publisher
    │
    │ RTX unavailable
    ▼
无法恢复历史 RTP
    │
    ▼
接收端根据自身状态
发送 PLI/FIR
    │
    ▼
   SFU
    │
    ▼
Publisher
    │
    ▼
Encoder
    │
    ▼
Keyframe
```

---

# 28. RTP Space Mapping 与 PeerConnection Isolation 的最终区别

这是整个讨论中最容易混淆、也是最值得记住的一点。

| 机制 | 作用 |
|---|---|
| RTP Sequence-Space Mapping | 解决“Subscriber 使用的 RTP SSRC/Seq 如何映射回 Publisher RTP SSRC/Seq” |
| PeerConnection Isolation | 解决“这个 RTCP feedback 属于哪个 Subscriber” |
| RTP Cache | 解决“SFU 能不能直接恢复历史 RTP” |
| Publisher RTX History | 解决“SFU Cache 没有时，Publisher 能不能继续恢复历史 RTP” |
| PLI/FIR | 解决“历史 RTP 已经无法恢复，需要重新发送关键帧” |

因此：

> **RTP Space Mapping 与 Subscriber Isolation 是两个不同问题。**

独立 PeerConnection 可以天然解决 Subscriber 的身份/会话隔离，因此在一个简单的 SFU 第一版里：

```text
不做 RTP Seq Mapping
+
保持 Publisher RTP SSRC/Seq
+
每个 Subscriber 独立 PC
+
SFU RTP Cache
+
per-subscriber retransmission
```

完全可以构成一个合理的设计。

---

# 29. 最终一句话总结

> **在 1→N SFU 中，Subscriber 的 NACK 是该 Subscriber 对自己下行接收状态的反馈，而不是 Publisher 的全局反馈。SFU 应该识别 NACK 的 Subscriber 归属，并优先通过本地 RTP Cache 对该 Subscriber 做针对性 retransmission；Cache Miss 后再向 Publisher 请求 NACK/RTX。由于每个 Subscriber 使用独立 PeerConnection，Session 本身可以提供天然隔离，因此 RTP sequence-space mapping 并非实现针对性 retransmission 的绝对前提。这样可以避免一个坏 Subscriber 的网络问题通过 NACK/RTX 传播给 Publisher 和其他网络正常的 Subscriber。**

---

## 30. 针对你当前 SFU 实现，推荐的实现优先级

如果按照你现在自己写 SFU 的路线，建议按这个顺序实现：

```text
① RTP 正常转发
        ↓
② 每 Subscriber 独立 PeerConnection
        ↓
③ RTCP RR / NACK 正确识别来源
        ↓
④ Publisher RTP Cache
        ↓
⑤ Subscriber NACK → Cache HIT → 本地 RTX
        ↓
⑥ Cache MISS → NACK → Publisher
        ↓
⑦ Publisher RTX → SFU → 指定 Subscriber
        ↓
⑧ PLI/FIR 正确路由
        ↓
⑨ 再考虑 RTP Sequence-Space Mapping
        ↓
⑩ 更复杂的 NACK aggregation / rate limiting / congestion control
```

其中 **①～⑧ 已经足够构成一个相当完整的 1→N SFU retransmission 基础架构**；RTP Space Mapping 可以根据你后续是否需要重写 SSRC、Seq、TWCC、MID/RID 等下行 RTP header 再决定是否引入。
