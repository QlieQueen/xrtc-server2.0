#include "video/video_receive_stream.h"

namespace xrtc {

VideoReceiveStream::VideoReceiveStream(const VideoReceiveStreamConfig& config) :
    config_(config),
    // 成员初始化顺序 = 声明顺序: 先创建统计模块, 再把它借给接收器
    rtp_receive_stat_(ReceiveStat::Create(config.clock)),
    rtp_video_stream_receiver_(config, rtp_receive_stat_.get())
{
}


VideoReceiveStream::~VideoReceiveStream() {
}

void VideoReceiveStream::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    rtp_video_stream_receiver_.OnRtpPacket(packet);
}

} // namespace xrtc
