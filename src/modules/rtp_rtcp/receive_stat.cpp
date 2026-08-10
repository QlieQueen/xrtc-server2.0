#include "modules/rtp_rtcp/receive_stat.h"

#include <rtc_base/logging.h>

namespace xrtc {


StreamStat::StreamStat(uint32_t ssrc, webrtc::Clock* clock) :
    ssrc_(ssrc),
    clock_(clock)
{

}

StreamStat::~StreamStat() {

}

void StreamStat::UpdateCounters(const webrtc::RtpPacketReceived& packet) {
    (void)packet;
}

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

// 取引用是关键: operator[] 对不存在的 key 会插入 value-initialized 值
// (unique_ptr → nullptr) 并返回其引用, 引用赋值即写回容器本身;
// 若用拷贝则创建只改局部, 容器永远为空
StreamStat* ReceiveStat::GetOrCreateStat(uint32_t ssrc) {
    std::unique_ptr<StreamStat>& stat = stats_[ssrc];
    if (nullptr == stat) {
        stat = std::make_unique<StreamStat>(ssrc, clock_);
    }

    return stat.get();
}

// 每个 RTP 包到达时调用, 按包里带的 ssrc 分流到对应统计桶,
// 4.4 的链路验证日志已注释, 统计逻辑从 4.6 起在 UpdateCounters 里填充
void ReceiveStat::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    //RTC_LOG(LS_WARNING) << "====================ssrc: " << packet.Ssrc()
    //    << ", sequence_number: " << packet.SequenceNumber();

    GetOrCreateStat(packet.Ssrc())->UpdateCounters(packet);
}


} // namespace xrtc 
