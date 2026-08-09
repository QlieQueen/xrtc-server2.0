#include "modules/rtp_rtcp/rtcp_receiver.h"

namespace xrtc {

RTCPReceiver::RTCPReceiver(const RtpRtcpConfig& config) {

}

RTCPReceiver::~RTCPReceiver() {

}

// 指针形式入口: 包装成ArrayView后交给统一解析入口
void RTCPReceiver::IncomingRtcpPacket(const uint8_t* packet, size_t packet_length) {
    IncomingRtcpPacket(rtc::MakeArrayView<const uint8_t>(packet, packet_length));
}

// 统一解析入口: 当前为框架空实现, 后续课程实现复合包拆包与SR/RR等类型分派
void RTCPReceiver::IncomingRtcpPacket(rtc::ArrayView<const uint8_t> packet) {

}


} // namespace xrtc
