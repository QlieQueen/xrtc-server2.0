# v2_3_10 RTCP 复合包接收解析（RTCPReceiver）

> 对应参考课程：`xrtc2.0-9.9/xrtcserver_v2_3.10`
> 本次改动：实现 `RTCPReceiver` 的 `ParseCompoundPacket` 拆包框架，打通 `IncomingRtcpPacket` → 复合包逐块解析 的接收链路（与 v2_3_7 的发送链路对称）

## 一、本节课解决的问题

`RTCPReceiver` 在 v2_3.8/v2_3.9 只是 `RtpRtcpImpl` 的一个空壳成员（`rtp_rtcp_impl.h:24` 实例化），`IncomingRtcpPacket` 没有任何解析逻辑。

RTCP 规范（RFC 3550）允许（通常也是）一个 UDP 报文中**拼接多个 RTCP 包**发送——即**复合包**（compound packet）。接收侧必须先把它按 4 字节公共头**逐个拆开**，才能按类型分派处理（SR/RR/PLI/NACK…）。本节课实现的 `ParseCompoundPacket` 就是这个拆包框架。

## 二、核心概念

### 1. 复合包结构与 4 字节公共头

```
//    0                   1           1       2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 0 |V=2|P|   C/F   |  Packet Type  |             length            |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                                                               |
//   |                       ......                                  |
//   |                                                               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// N |V=2|P|   C/F   |  Packet Type  |             length            |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

每个 RTCP 块以 4 字节公共头开头：

| 字段 | 含义 |
|------|------|
| V | 版本，必须 = 2，否则包非法 |
| P | padding 标志 |
| C/F | 5 bit：SR 里是报告块计数，PSFB 里是 fmt |
| Packet Type | SR=200、RR=201、SDES=202、BYE=203、RTPFB=205、PSFB=206… |
| length | 16 bit，**单位是 4 字节**，实际 payload = length × 4 |

### 2. CommonHeader::Parse —— 纯解析指针，成功返回 true

`CommonHeader` 定义在 rtcbase：`src/modules/rtp_rtcp/source/rtcp_packet/common_header.{h,cc}`。`Parse` 不拷贝数据，只做三件事：

1. 校验：字节数 < 4 / 版本 ≠ 2 / padding 非法 / 声明长度越界 → **返回 false**
2. 记录 `payload_ = buffer + 4`（**已经跳过 4 字节头**）
3. 记录 `payload_size_`（= length×4 再减 padding）、`packet_type_`、`count_or_format_`

**返回值：`true` = 解析成功**（common_header.cc 只有最后一条路径返回 true，其余全是 false）。

### 3. NextPacket() —— 下一块起点，只在 Parse 成功后可信

```cpp
const uint8_t* NextPacket() const {
  return payload_ + payload_size_ + padding_size_;
}
```

- 返回**指针**（不是偏移量），含义 = "当前块结束位置 = 下一块起点"
- **为什么不用加头部 4 字节**：`payload_` 在 Parse 里已经被赋值为 `buffer + 4`，头部已包含在内
- 对照理解：`packet_size() = kHeaderSizeBytes(4) + payload_size_ + padding_size_` 是整包长度，`NextPacket() = 当前块起点 + packet_size()`
- 每个 RTCP 块 4 字节对齐（length 单位是 4 字节），所以 NextPacket() 永远落在下一块头部边界

### 4. 光标与解析状态互相配合（迭代接力）

```
next_block（光标） ──指定解析位置──► rtcp_block.Parse(...)
      ▲                                    │
      │                                    ▼
      └──── NextPacket() 算出下一块起点 ◄── 解析成功（拿到 payload_/payload_size_）
```

- `next_block` 是输入：告诉 Parse 从哪个地址解析
- `rtcp_block` 是状态：Parse 把该位置的头信息记录在它自己身上
- 推进靠状态回喂：NextPacket() 用解析结果算出新位置，下一轮再读

## 三、解析流程

```
IncomingRtcpPacket(ptr, len)            指针入口（薄包装）
  └─ IncomingRtcpPacket(ArrayView)      统一入口
       ├─ packet.empty()? → 直接 return   Parse 至少需要 4 字节
       ├─ ParseCompoundPacket()
       │    for next_block = begin; != end; next_block = NextPacket():
       │        Parse(当前位置, 剩余字节)
       │        成功 → 后续课程在此按 type 分派（SR/RR/…）
       │        失败 → 首块?      return false     ← 整体丢弃
       │               非首块?    ++num_skipped_packet_; break  ← 保留已解析，放弃剩余
       └─ 解析结果装入 packet_information（当前为空壳）
```

**失败为什么必须立即终止**：`NextPacket()` 依赖 Parse 成功时设置的 `payload_`；失败时它可能是 `nullptr`（版本校验失败时根本没赋值）或残留值，基于不可信状态推进会越界/死循环。这就是"首块失败 return false、中间块失败 break"两个分支存在的根本原因。

## 四、设计细节

1. **PacketInformation 用 pimpl**：`.h` 里只前向声明（rtcp_receiver.h:22），定义藏在 `.cpp`（rtcp_receiver.cpp:8）。后续课往里加 SR 的 NTP 时间戳、RR 丢包统计等字段时，头文件不用动
2. **双 IncomingRtcpPacket 重载**：指针+长度版 `MakeArrayView` 转成 ArrayView 后走统一入口，避免上层到处处理空指针
3. **入口先查 empty**：`CommonHeader::Parse` 要求至少 4 字节，空包提前拒绝
4. **num_skipped_packet_**：统计"非首块解析失败被丢弃"的包数，排查线上脏包用

## 五、当前状态与后续

- `IncomingRtcpPacket` **目前没有任何调用方**（`rtcp_receiver_` 只是 RtpRtcpImpl 的成员，实例化即止），本课无可观察的外部行为
- 后续课：在拆包循环里按 `rtcp_block.type()` 分派 → SR/RR/PLI/NACK 解析；再接入接收路径（SRTP 解密后调用 IncomingRtcpPacket）
- 本课联调验证 = 编译通过 + 推拉流回归正常，无需客户端配合

## 六、课程差异记录（踩坑）

1. **关键 bug：`if (rtcp_block.Parse(...))` 漏了 `!`**。Parse 成功返回 true，正确写法是 `if (!Parse(...))` 处理**失败**分支。漏掉后：
   - 合法首块 → Parse 返回 true → 走进分支 → 误判 `return false`，**所有合法 RTCP 包全部被丢弃**（必然路径，不是边缘情况）
   - 非法首块 → Parse 返回 false → 不走进分支 → 循环继续用不可信的 NextPacket() 推进 → 越界/死循环风险
   - 已修复（rtcp_receiver.cpp:68）
2. **include 位置**：参考把 `common_header.h` 放在 `.h` 里，本工程放 `.cpp`——解析类型仅实现需要，不外泄更合理
3. **变量名**：参考 `num_skipped_packets_`（复数），本工程 `num_skipped_packet_`，功能一致
4. 本工程补充了关键注释（NextPacket 可信性、payload_ 已跳过 4 字节头、失败必须 break 的原因）
