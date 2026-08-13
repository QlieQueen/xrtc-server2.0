#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_

#include <system_wrappers/include/clock.h>

#include "base/event_loop.h"
#include "modules/rtp_rtcp/receive_stat.h"

namespace xrtc {

struct RtpRtcpConfig {
    EventLoop* el = nullptr;
    webrtc::Clock* clock = nullptr;
    bool audio = false;
    uint32_t local_media_ssrc = 0;
    ReceiveStat* receive_stat = nullptr; // 接收统计指针(4.10 接入), 传给 RTCPSender 作 RR 报告块数据源
    absl::optional<uint32_t> rtcp_report_interval_ms;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_

