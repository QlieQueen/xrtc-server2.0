#include "modules/rtp_rtcp/rtcp_sender.h"

#include <rtc_base/logging.h>

namespace xrtc {

RTCPSender::RTCPSender(const RtpRtcpConfig& config) :
    clock_(config.clock)
{

}

RTCPSender::~RTCPSender() {

}

// 设置RTCP发送模式: kOff 关闭 / kCompound 复合包 / kNonCompound 单个包
void RTCPSender::SetRtcpStatus(webrtc::RtcpMode method) {
    method_ = method;
}

// 把某个 RTCP 报文类型标记为待发送, is_volatile=true 表示一次性标记(发送完就删除)
void RTCPSender::SetFlag(uint32_t type, bool is_volatile) {
    report_flags_.insert(ReportFlag(type, is_volatile));
}

// 发送RTCP报文: 入口函数, 根据报文类型计算并发送复合RTCP包
void RTCPSender::SendRTCP(webrtc::RTCPPacketType packet_type) {
    auto result = ComputeCompundRTCPPacket(packet_type);
}

// 计算复合RTCP包: 先把本次要发送的报文类型记入 report_flags_,
// 后续再遍历集合逐项组装报文并发送(集合中非volatile的项会常驻, 周期性发送)
absl::optional<uint32_t> RTCPSender::ComputeCompundRTCPPacket(
        webrtc::RTCPPacketType packet_type)
{
    // RTCP被关闭时直接返回, 不发送任何报文
    if (method_ == webrtc::RtcpMode::kOff) {
        RTC_LOG(LS_WARNING) << "cannot send rtcp if it is disabled";
        return -1;
    }

    // 标记为volatile, 表示本次发送的报文类型只在集合中保留一次
    SetFlag(packet_type, true);

    return absl::nullopt;
}

}
