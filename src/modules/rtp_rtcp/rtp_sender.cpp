#include "modules/rtp_rtcp/rtp_sender.h"

namespace xrtc {

RtpSender::RtpSender(const RtpRtcpConfig& config) :
    config_(config)
{

}

RtpSender::~RtpSender() {}

void RtpSender::UpdateRtpStat(int64_t now_ms, const webrtc::RtpPacketToSend& packet) {
    webrtc::RtpPacketCounter counter(packet);

    webrtc::StreamDataCounters* counters =
        packet.Ssrc() == config_.rtx_send_ssrc ? &rtx_rtp_stats_ : &rtp_stats_; 

    if (counters->first_packet_time_ms == -1) {
        counters->first_packet_time_ms = now_ms;
    }

    counters->transmitted.Add(counter);
}

void RtpSender::GetDataCounters(webrtc::StreamDataCounters* rtp_stats,
        webrtc::StreamDataCounters* rtx_rtp_stats)
{
    *rtp_stats = rtp_stats_;
    *rtx_rtp_stats = rtx_rtp_stats_;
}


} // namespace xrtc

