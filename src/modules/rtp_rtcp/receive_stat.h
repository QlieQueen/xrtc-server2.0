#ifndef XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_
#define XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_

#include <memory>

#include <rtc_base/containers/flat_map.h>
#include <system_wrappers/include/clock.h>
#include <modules/rtp_rtcp/source/rtp_packet_received.h>

namespace xrtc {

// per-SSRC 统计桶: 每个 SSRC 一个独立实例, 各自累计自己的
// 序列号/到达时间. 主媒体流与 RTX 流的 seq 独立递增,
// 必须分桶统计, 否则后续丢包/jitter 计算会串流
class StreamStat {
public:
    StreamStat(uint32_t ssrc, webrtc::Clock* clock);
    ~StreamStat();

    // 每包累计计数, 当前空壳, 4.6 填充 seq 解绕/丢包统计
    void UpdateCounters(const webrtc::RtpPacketReceived& packet);

private:
    uint32_t ssrc_;
    webrtc::Clock* clock_;
};

// 接收统计模块: 为 RTCP RR (Receiver Report) 提供数据源.
// 每个到达的 RTP 包调一次 OnRtpPacket, 累计序列号/到达时间等原始数据,
// 后续课程在此基础上统计丢包/乱序/jitter, 并生成 RR 的 report blocks (4.10)
class ReceiveStat {
public:
    ReceiveStat(webrtc::Clock* clock);
    ~ReceiveStat();

    // 工厂方法: 统一入口创建统计模块
    static std::unique_ptr<ReceiveStat> Create(webrtc::Clock* clock);
    void OnRtpPacket(const webrtc::RtpPacketReceived& packet);

    // 按 ssrc 取统计桶, 不存在则惰性创建 (SSRC 是 answer 协商后才知道的,
    // 主媒体与 rtx 两个 ssrc 知晓时机不同, 按需创建)
    StreamStat* GetOrCreateStat(uint32_t ssrc);

private:
    webrtc::Clock* clock_;   // 时钟: 记录包到达时间, jitter 计算的时间基准
    // ssrc → 统计桶. flat_map: 小容器连续内存 + 二分查找,
    // SSRC 数量极小(1~2个), 比 unordered_map 缓存友好
    webrtc::flat_map<uint32_t, std::unique_ptr<StreamStat>> stats_;
};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_
