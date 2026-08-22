#ifndef __XRTCSERVER_VIDEO_VIDEO_SEND_STREAM_H_
#define __XRTCSERVER_VIDEO_VIDEO_SEND_STREAM_H_

#include "video/video_send_stream_config.h"
#include "modules/rtp_rtcp/rtp_rtcp_impl.h"

namespace xrtc {

class VideoSendStream {
public:
    VideoSendStream(const VideoSendStreamConfig& config);
    ~VideoSendStream();

    void UpdateRtpStat(int64_t now_ms, const webrtc::RtpPacketToSend& packet);

private:
    VideoSendStreamConfig config_;
    std::unique_ptr<RtpRtcpImpl> rtp_rtcp_;
};


} // namespace xrtc

#endif // __XRTCSERVER_VIDEO_VIDEO_SEND_STREAM_H_
