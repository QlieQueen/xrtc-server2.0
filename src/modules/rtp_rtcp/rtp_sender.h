#ifndef XRTCSERVER_MODULES_RTP_RTCP_RTP_SENDER_H_
#define XRTCSERVER_MODULES_RTP_RTCP_RTP_SENDER_H_

#include "modules/rtp_rtcp/rtp_rtcp_config.h"

#include <modules/rtp_rtcp/source/rtp_packet_to_send.h>

namespace xrtc {

class RtpSender {
public:
    RtpSender(const RtpRtcpConfig& config);
    ~RtpSender();

    void UpdateRtpStat(int64_t now_ms, const webrtc::RtpPacketToSend& packet);
    void GetDataCounters(webrtc::StreamDataCounters* rtp_stats,
            webrtc::StreamDataCounters* rtx_rtp_stats);

private:
    RtpRtcpConfig config_;
    webrtc::StreamDataCounters rtp_stats_;
    webrtc::StreamDataCounters rtx_rtp_stats_;
};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_RTP_RTCP_RTP_SENDER_H_
