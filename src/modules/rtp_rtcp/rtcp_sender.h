#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_

#include <modules/rtp_rtcp/include/rtp_rtcp_defines.h>

#include "modules/rtp_rtcp/rtp_rtcp_config.h"

namespace xrtc {

class RTCPSender {
public:
    RTCPSender(const RtpRtcpConfig& config);
    ~RTCPSender();

    void SendRTCP(webrtc::RTCPPacketType packet_type);

private:
    webrtc::Clock* clock_;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_
