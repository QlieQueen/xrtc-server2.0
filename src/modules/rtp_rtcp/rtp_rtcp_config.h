#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_

#include <system_wrappers/include/clock.h>

#include "base/event_loop.h"

namespace xrtc {

struct RtpRtcpConfig {
    EventLoop* el = nullptr;
    webrtc::Clock* clock = nullptr;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_

