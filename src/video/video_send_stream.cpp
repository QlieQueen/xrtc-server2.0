#include "video/video_send_stream.h"

namespace xrtc {

VideoSendStream::VideoSendStream(const VideoSendStreamConfig& config) :
    config_(config)
{
    RtpRtcpConfig rr_config;
    rr_config.el = config.el;
    rr_config.clock = config.clock;
    rr_config.rtp_rtcp_module_observer = config.rtp_rtcp_module_observer;
    rr_config.local_media_ssrc = config.rtp.local_ssrc;
    rr_config.rtx_send_ssrc = config.rtp.local_rtx_ssrc;

    rtp_rtcp_ = std::make_unique<RtpRtcpImpl>(rr_config);
    rtp_rtcp_->SetRtcpStatus(webrtc::RtcpMode::kCompound);
    rtp_rtcp_->SetSendingStatus(true);
}

VideoSendStream::~VideoSendStream() {

}

void VideoSendStream::UpdateRtpStat(int64_t now_ms, const webrtc::RtpPacketToSend& packet) {
    rtp_rtcp_->UpdateRtpStat(now_ms, packet);
}


} // namespace xrtc
