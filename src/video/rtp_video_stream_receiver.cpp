#include "video/rtp_video_stream_receiver.h"

namespace xrtc {

RtpVideoStreamReceiver::RtpVideoStreamReceiver(const VideoReceiveStreamConfig& config,
        ReceiveStat* rtp_receive_stat) :
    config_(config),
    rtp_receive_stat_(rtp_receive_stat)
{

}

RtpVideoStreamReceiver::~RtpVideoStreamReceiver() {

}

void RtpVideoStreamReceiver::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    // recovered() = 由 RTX/重传恢复出的包. 接收统计只应反映"真实到达"的包,
    // 重传恢复的包若不排除, 会歪曲丢包率/jitter 的计算
    if (!packet.recovered()) {
        rtp_receive_stat_->OnRtpPacket(packet);
    }
}

} // namespace xrtc
