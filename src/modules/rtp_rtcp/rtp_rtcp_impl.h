#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_

#include "modules/rtp_rtcp/rtp_rtcp_config.h"

namespace xrtc {

class RtpRtcpImpl {
public:
    RtpRtcpImpl(const RtpRtcpConfig& config);
    ~RtpRtcpImpl();

private:
    EventLoop* el_;
};  

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_

