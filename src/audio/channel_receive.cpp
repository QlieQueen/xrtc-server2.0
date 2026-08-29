#include "audio/channel_receive.h"

namespace xrtc {

ChannelReceive::ChannelReceive(const AudioReceiveStreamConfig& config) :
    config_(config)
{

}

ChannelReceive::~ChannelReceive() {

}

void ChannelReceive::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    if (config_.rtp_rtcp_module_observer) {
        config_.rtp_rtcp_module_observer->OnRtpPacket(webrtc::MediaType::AUDIO,
                packet);
    }

    // 进行统计

}

} // namespace xrtc
