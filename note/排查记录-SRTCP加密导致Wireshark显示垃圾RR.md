# 排查记录：Wireshark 抓到的 RR 包显示垃圾值（真相是 SRTCP 加密）

> 日期：2026-08-13 ~ 08-14，v2_4.17 联调期间
> 结论一句话：**RR 包本身健康，Wireshark 没 DTLS 密钥，把 SRTCP 密文当明文解析，所以看到一堆变来变去的垃圾值**

## 一、现象

- SFU 日志（RR-build 打印）显示健康值：`cumulative_loss_: 0`、`ext_seq: 1363→1460`、ssrc 与 answer 一致
- Wireshark 抓包却显示垃圾：media ssrc 乱值、cum lost 负几十万/正十几万、jitter 35 亿、LSR 是 2022 年的 Unix 时间、DLSR ≈ 10.8 小时
- **每次抓包垃圾值都不一样**，永远对不上日志——这就是"每次 IV 不同 → 密文每次都是新随机数"

## 二、排查过程（走过的弯路）

1. 先怀疑旧进程/外部发送者 → 被 5-tuple 匹配否定（10025→55684 与 ICE 日志完全一致，包就是自己服务器发的）
2. 怀疑客户端 → 客户端是标准 libwebrtc，行为可信
3. 怀疑加密 → 看代码发现 is_dtls_ 有 false 分支（v2_2.x 去 DTLS 功能），误以为部署可能是明文 → 走进死胡同
4. 关键转折：云端 Claude Code 抓包分析，发现包长 46 字节且 SRTCP index 的 E 位递增 → 加密实锤

## 三、真相

- **DTLS-SRTP 是本项目的常态**：默认 `is_dtls_ = true`（transport_controller.h:84）；客户端是标准 libwebrtc，必然协商 DTLS 密钥，后续 RTP/RTCP 收发都要加解密——**与本地/云端无关**
- v2_2.x 的"去 DTLS 明文通路"（`is_dtls_ = false`）是配合**课程作者手写客户端**用的（手写端可省 DTLS）；原生 libwebrtc 客户端 DTLS 必开，一旦指定 is_dtls=false 反而连不上——本项目用原生 libwebrtc 客户端，is_dtls 恒 true，该开关实际无作用
- 排查时误以为"可能明文"是最大的坑：明文模式在本项目的客户端组合下根本不存在

## 四、判定方法（下次直接对照）

| 特征 | 明文 RTCP RR | SRTCP（加密） |
|------|-------------|--------------|
| 包长 | 32 字节（8 头 + 24 report block） | **46 字节**（32 + 4 SRTCP index + 10 HMAC-SHA1-80 认证标签） |
| 头部 | 可解析出真实字段 | 前 8 字节明文（如 `81c9 0007` + sender ssrc），其余加密 |
| index 区 | 无 | 4 字节，bit31（E 位）= 1，逐包递增 |
| Wireshark 解析 | 正确 | 把密文当明文，字段全是随机值 |

参考：客户端→SFU 的 SR 是 70 字节（SR 28 + SDES 28 + index 4 + tag 10），同理加密。

## 五、验证手段（按可靠性排序）

1. **客户端侧打印**（最可靠）：客户端 libwebrtc 有 DTLS 密钥能解密，`getStats()` 的 `remote-inbound-rtp` 就是 RR 解析结果。实测健康：`packets_lost: 0, fraction_lost: 0, jitter: 0.012, rtt_ms: 44~77`。代码已注释保留在客户端 `sdk/base/metrics.cpp`（含 `webrtc_stream.cpp` 启动条件说明），需要时打开注释
2. 关 DTLS 抓一次包（`PeerConnection::Init` 不传证书）→ 立刻明文
3. Wireshark 配 `SSLKEYLOGFILE` 导出密钥解密

## 六、副产品发现

- SFU 日志 `unknown rtcp packet_type: 202`：202 是 SDES（CNAME），客户端发 SR+SDES 复合包，服务器只处理 SR/RR，无害
- 云端持续发异常 64 字节 STUN 包（ICE-CONTROLLED 长度 20、无冒号 USERNAME）——疑似课程自研 STUN 构建器残留，独立问题未处理
- `HandleRr` 收到客户端 RR 静默丢弃（rtcp_receiver.cpp:171-175）——排查问题时容易漏线索，建议补日志

## 七、相关代码位置

- `peer_connection.cpp:153` `Init`：certificate 决定 is_dtls_（恒传证书 → 恒 DTLS-SRTP；is_dtls=false 仅配合课程手写客户端，原生 libwebrtc 客户端用不到）
- `transport_controller.cpp:326` `SendRtcp`：is_dtls_ 分支选 DtlsSrtpTransport / ICE channel
- `rtcp_sender.cpp` `BuildRR` + `CreateRtcpReportBlocks`：RR 构建（值一直是健康的）
- `rtcp_receiver.cpp:171-175`：HandleRr 静默丢弃
