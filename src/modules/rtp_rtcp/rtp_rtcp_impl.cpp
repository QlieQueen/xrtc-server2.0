#include "rtp_rtcp_impl.h"

namespace xrtc {

RtpRtcpImpl::RtpRtcpImpl(const RtpRtcpConfig& config) :
    el_(config.el),
    rtcp_sender_(config)
{

}

RtpRtcpImpl::~RtpRtcpImpl() {

}

} // namespace xrtc
