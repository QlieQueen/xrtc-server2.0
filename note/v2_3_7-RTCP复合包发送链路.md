# v2_3_7 RTCP 复合包发送链路（PacketSender）

> 对应参考课程：`xrtc2.0-9.9/xrtcserver_v2_3.7`
> 本次改动：引入 `PacketSender` 内部类，打通 `SendRTCP` → 调度 → builder → 序列化 → 发送 的完整链路

## 一、本节课解决的问题

前几节课的 `RTCPSender` 只有"决定发什么"（report_flags_ / PrepareReport）和"分派"（builders_）的骨架，`BuildRR` 是空实现，没有任何"把包真正发出去"的机制。

RTCP 规范（RFC 3550）要求报文**不能单独裸发**，要把多个 RTCP 包（SR/RR + SDES + …）拼成一个 UDP 报文再发——即**复合包**（compound packet）。本节课引入的 `PacketSender` 就是复合包的组装器 + 发送器。

## 二、核心概念

### 1. PacketSender（复合包打包器）

```
packet 1 → AppendPacket → ┌─────────────────────┐
packet 2 → AppendPacket → │  buffer_[IP_PACKET_SIZE]  │ ← 游标 index_ 记录写入位置
packet 3 → AppendPacket → └─────────────────────┘
                               │
                          Send() 一次性交给回调
```

- `AppendPacket(packet)`：把单个 RTCP 包序列化进 `buffer_[index_]` 处，`index_` 后移——相当于往复合包里追加一个包
- `Send()`：`index_ > 0` 时把整块 buffer 通过回调交出去（即一个 UDP 报文），然后 `index_` 归零

### 2. RtcpPacket::Create 与回调（自动分包）

`webrtc::rtcp::RtcpPacket` 是所有 RTCP 包（SR/RR/NACK/PLI…）的基类，`Create` 是虚函数，每个具体包类型重写自己的字节布局。AppendPacket 内部调用：

```cpp
packet.Create(buffer_, &index_, max_packet_size_, callback_);
```

传回调的原因：**复合包累加超过 max_packet_size_ 时，Create 内部（OnBufferFull）先把已填满的部分通过回调发走，index_ 归零后同一块 buffer 继续复用**——即自动分包，保证每个 UDP 报文不超过 MTU。

**分包发生在"包与包之间"，单包永不跨报文**（这是最容易绕晕的点，拆开看）：

```cpp
// 每个子类的 Create 都是同一套先腾空、后整写的模式（以 SenderReport 为例）：
while (*index + BlockLength() > max_length) {   // ① 已写 + 本次包长 > 上限 → 触发回调
    if (!OnBufferFull(packet, index, callback))  // 发走 [0, *index)，*index = 0
        return false;
}
CreateHeader(..., packet, index);                 // ② 本次包从 index(=0) 处完整写入
...
*index += kSenderBaseLength;                      // ③ 一个字节不少
```

- ① `OnBufferFull` 发走的是 **buffer_ 里已有的全部（之前的包们）**，随后 `*index = 0`
- ②③ 因为已归零，**本次这个包整体写入新的 buffer**——它只是"挤爆空间"的触发者，自己等在 while 外面
- 所以**拆包只发生在包与包之间**（A/B 一个报文，C 下个报文），**单个 RTCP 包绝不会被切成两半**——这符合 RTCP 规范：一个 UDP 报文里必须是完整的 RTCP 块
- 冷知识：`OnBufferFull` 里有 `if (*index == 0) return false;`——若单包本身超过 `max_length`（buffer 还是空的就被调），直接返回 false 中断，防死循环。这也是 `max_packet_size_` 要留余量的原因

### 3. 回调（callback_）的触发时机

| 时机 | 触发者 | 含义 |
|------|--------|------|
| 过程中 buffer 满了 | `Create` 内部（OnBufferFull） | 中途 flush，发走一个完整的 UDP 报文 |
| 复合完成 | `Send()` | 发走剩余内容 |

**上层无法区分是"中间片段"还是"最终包"**——收到回调就发。当前课程的回调是打日志（`RTC_LOG` 打印包大小），后续课程会替换为真正的网络发送。

**关键理解：一次回调 = 一个完整的 RTCP 复合包 = 将来要发的 1 个 UDP 报文。**

```cpp
auto callback = [&](rtc::ArrayView<const uint8_t> packet) {
    // packet 指向 buffer_ 的 [0, index_)，是已拼好的完整复合包：
    // 首块必是 SR/RR（复合包规则），后面跟着本次 report_flags_ 里的所有块
    RTC_LOG(LS_WARNING) << "====================build rtcp packet, size: " << packet.size();
};
```

两个容易忽略的点：

1. **回调参数是"视图"不是"拷贝"**：`packet` 指向 `PacketSender::buffer_` 内部，`OnBufferFull` 回调完立刻 `*index = 0`，buffer 会被下一个复合包复用覆盖。所以将来接网络发送时**必须在回调内部同步发出**：
   ```cpp
   auto callback = [&](rtc::ArrayView<const uint8_t> packet) {
       transport_->SendRtcp(packet.data(), packet.size());  // 立即发出，不能延迟
   };
   ```
2. **回调只发"完整复合包"，绝不发半截**：因为分包发生在包与包之间（见上节），回调被触发时 buffer 里一定是一个或多个完整的 RTCP 块——与"一个 UDP 报文必须装完整块"的协议要求吻合。

### 4. max_packet_size_ = IP_PACKET_SIZE - 28

- `IP_PACKET_SIZE` = **1500**（`rtp_rtcp_defines.h:32` 定义，以太网 MTU 假设）
- 减 28 = IP 头 20B + UDP 头 8B
- 结果 1472B：UDP 载荷上限，保证一个 RTCP 报文恰好一个以太网帧，不触发 IP 分片

## 三、调用链

```
SendRTCP(packet_type)                             入口
 ├─ 创建 lambda 回调（当前打日志，将来真正发送）
 ├─ sender = PacketSender(max_packet_size_, 回调)   打包器（1472B 缓冲）
 ├─ ComputeCompoundRTCPPacket(type, sender)        调度中心
 │    ├─ SetFlag(type, true)                       ① 记录"本次要发这种报文"
 │    ├─ PrepareReport()                           ② 按模式决定附带 SR/RR
 │    └─ for 遍历 report_flags_:
 │         └─ builders_[type](sender)              ③ 调 BuildRR(sender)
 │              └─ sender.AppendPacket(packet)     包对象自己序列化进 buffer
 │                   └─ 满了? → 回调 flush, index_ 归零复用
 ├─ sender->Send()                                 ④ 缓冲剩余内容一次性发出
 └─ return 0
```

核心分工：
- **SendRTCP** 管发送时机（创建 sender、最后 Send）
- **ComputeCompoundRTCPPacket** 管"发什么"（SetFlag + PrepareReport 填充 report_flags_，遍历分派 builder）
- **BuildXX** 管"怎么把单个包装进复合包"（构造包对象 → AppendPacket）

## 四、设计细节

1. **builder 不直接写 buffer**：`BuildRR(sender)` 构造包对象后调 `sender.AppendPacket(rr)`，由包对象自己的 `Create` 虚函数序列化——builder 只负责"造包 + 交给 sender"
2. **absl::optional<PacketSender>**：PacketSender 无默认构造（只有带参构造），optional 允许"先声明、拿到 max_packet_size_ 和 callback 后再 emplace"，WebRTC 原版风格；直接栈上构造亦可
3. **返回值链路**：kOff 时 `ComputeCompoundRTCPPacket` 返回 -1，`SendRTCP` 里 `if (result) return *result;` 提前退出跳过 `Send()`；正常路径返回 `nullopt`，最后 `sender->Send()`，返回 0
4. **BuilderFunc 带 PacketSender& 参数**：每个 builder 都要把结果追加进复合包，所以必须拿到 sender

## 五、当前状态与后续

**实际走一遍 `SendRTCP(kRtcpRr)`（kCompound）**：
1. `SetFlag(kRtcpRr, true)` → 集合 `{RR(volatile)}`
2. `PrepareReport()`：已有 RR → 不重复添加
3. 遍历 → 调 `BuildRR(sender)` —— **空实现，什么都没追加，index_ 仍为 0**
4. `sender->Send()`：`index_ == 0` → **什么都不发**

即：链路已打通但还发不出包，因为 `BuildRR` 里还没有 `AppendPacket`。下一节课填充 `BuildRR`（构造 RR 包、填接收统计），打包链路才真正闭合。

## 六、课程差异记录

- 参考代码 v2_3.7 的 report_flags_ 遍历仍是"先清理后取 type"的错位写法（第一个元素丢失、可能解引用 end()）；本工程已按 v2_9.9 修正：**先保存 `it->type` → 再清理 → 后用保存的 type 构建**
- 本工程 `builders_` 注册 `kRtcpRr → BuildRR`（与参考一致）；最终版 v2_9.9 中 `kRtcpSr → BuildSR`、`kRtcpNack → BuildNack`、`kRtcpPli → BuildPli` 等后续课程逐个注册
