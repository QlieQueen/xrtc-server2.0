#include "modules/rtp_rtcp/rtcp_sender.h"

namespace xrtc {

RTCPSender::RTCPSender(const RtpRtcpConfig& config) :
    clock_(config.clock)
{

}

RTCPSender::~RTCPSender() {

}

void RTCPSender::SendRTCP(webrtc::RTCPPacketType packet_type) {
    // todo
}

};
