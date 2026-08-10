#include "modules/rtp_rtcp/receive_stat.h"

#include <rtc_base/logging.h>

namespace xrtc {

ReceiveStat::ReceiveStat(webrtc::Clock* clock) :
    clock_(clock)
{

}

ReceiveStat::~ReceiveStat() {

}

// 工厂方法: 统一入口创建统计模块
std::unique_ptr<ReceiveStat> ReceiveStat::Create(webrtc::Clock* clock) {
    return std::make_unique<ReceiveStat>(clock);
}

// 每个 RTP 包到达时调用, 累计接收统计.
// 当前课程只打日志验证链路, 统计项(丢包/jitter/RR)留待后续填充
void ReceiveStat::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    RTC_LOG(LS_WARNING) << "====================ssrc: " << packet.Ssrc()
        << ", sequence_number: " << packet.SequenceNumber();
}


} // namespace xrtc 
