#include "video/video_receive_stream.h"

namespace xrtc {

VideoReceiveStream::VideoReceiveStream(const VideoReceiveStreamConfig& config) :
    config_(config),
    // 成员初始化顺序 = 声明顺序: 先创建统计模块, 再把它借给接收器
    rtp_receive_stat_(ReceiveStat::Create(config.clock)),
    rtp_video_stream_receiver_(config, rtp_receive_stat_.get())
{
    if (config.rtp.rtx_ssrc != 0) {
        rtx_receive_stream_ = std::make_unique<RtxReceiveStream>(
            &rtp_video_stream_receiver_,
            config.rtp.remote_ssrc,
            config.rtp.rtx_associated_payload_types,
            rtp_receive_stat_.get());
    }
}


VideoReceiveStream::~VideoReceiveStream() {
}

void VideoReceiveStream::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    if (packet.Ssrc() == config_.rtp.rtx_ssrc) {
        if (rtx_receive_stream_) {
            rtx_receive_stream_->OnRtpPacket(packet);
        }
    } else {
        rtp_video_stream_receiver_.OnRtpPacket(packet);
    }
}

// 收到RTCP数据: 转给RtpVideoStreamReceiver, 最终喂给RTCP模块解析
void VideoReceiveStream::DeliverRtcp(const uint8_t* data, size_t len) {
    rtp_video_stream_receiver_.DeliverRtcp(data, len);
}

} // namespace xrtc
