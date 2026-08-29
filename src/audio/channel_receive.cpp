#include "audio/channel_receive.h"

namespace xrtc {

ChannelReceive::ChannelReceive(const AudioReceiveStreamConfig& config,
        ReceiveStat* receive_stat) :
    config_(config),
    rtp_receive_stat_(receive_stat)
{
    RtpRtcpConfig rr_config;
    rr_config.el = config.el;
    rr_config.clock = config.clock;
    rr_config.audio = true;
    rr_config.local_media_ssrc = config.rtp.local_ssrc;
    rr_config.receive_stat = rtp_receive_stat_;
    rr_config.rtp_rtcp_module_observer = config.rtp_rtcp_module_observer;

    rtp_rtcp_ = std::make_unique<RtpRtcpImpl>(rr_config);
    rtp_rtcp_->SetRemoteSsrc(config.rtp.remote_ssrc);
    rtp_rtcp_->SetRtcpStatus(webrtc::RtcpMode::kCompound);
}

ChannelReceive::~ChannelReceive() {

}

void ChannelReceive::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    if (config_.rtp_rtcp_module_observer) {
        config_.rtp_rtcp_module_observer->OnRtpPacket(webrtc::MediaType::AUDIO,
                packet);
    }

    // 进行统计
    if (rtp_receive_stat_) {
        rtp_receive_stat_->OnRtpPacket(packet);
    }
}

void ChannelReceive::DeliverRtcp(const uint8_t* data, size_t len) {
    rtp_rtcp_->IncomingRtcpPacket(data, len);
}

} // namespace xrtc
