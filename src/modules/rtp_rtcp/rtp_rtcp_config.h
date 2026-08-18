#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_

#include <system_wrappers/include/clock.h>
#include <modules/rtp_rtcp/source/rtp_packet_received.h>

#include "base/event_loop.h"
#include "modules/rtp_rtcp/receive_stat.h"
#include "modules/video_coding/rtp_frame_object.h"

namespace xrtc {

class RtpRtcpModuleObserver {
public:
    virtual ~RtpRtcpModuleObserver() {}

    virtual void OnRtpPacket(webrtc::MediaType media_type,
            const webrtc::RtpPacketReceived& packet) = 0;
    virtual void OnLocalRtcpPacket(webrtc::MediaType media_type,
            const uint8_t* data, size_t len) = 0;
    virtual void OnFrame(std::unique_ptr<RtpFrameObject> frame) = 0;
};

struct RtpRtcpConfig {
    EventLoop* el = nullptr;
    webrtc::Clock* clock = nullptr;
    bool audio = false;
    uint32_t local_media_ssrc = 0;
    ReceiveStat* receive_stat = nullptr; // 接收统计指针(4.10 接入), 传给 RTCPSender 作 RR 报告块数据源
    absl::optional<uint32_t> rtcp_report_interval_ms;
    RtpRtcpModuleObserver* rtp_rtcp_module_observer = nullptr;
    int request_pli_interval_ms = 0;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_CONFIG_H_

