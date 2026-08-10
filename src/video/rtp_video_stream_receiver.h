#ifndef XRTCSERVER_VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_
#define XRTCSERVER_VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_

#include <modules/rtp_rtcp/source/rtp_packet_received.h>

#include "video/video_receive_stream_config.h"

namespace xrtc {

// 视频RTP包接收器: 对应WebRTC VideoReceiveStream内部的同名类,
// 负责接收RTP包并维护接收统计(序列号回绕/丢包/jitter), 触发RTCP RR
// 当前为骨架, 接收逻辑后续课程填充
class RtpVideoStreamReceiver {
public:
    RtpVideoStreamReceiver(const VideoReceiveStreamConfig& config);
    ~RtpVideoStreamReceiver();

    void OnRtpPacket(const webrtc::RtpPacketReceived& packet);

private:
    VideoReceiveStreamConfig config_;
};

} // namespace xrtc

#endif // XRTCSERVER_VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_
