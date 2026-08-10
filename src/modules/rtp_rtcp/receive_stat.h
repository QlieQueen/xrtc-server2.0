#ifndef XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_
#define XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_

#include <memory>

#include <system_wrappers/include/clock.h>
#include <modules/rtp_rtcp/source/rtp_packet_received.h>

namespace xrtc {

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

private:
    webrtc::Clock* clock_;   // 时钟: 记录包到达时间, jitter 计算的时间基准

};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_
