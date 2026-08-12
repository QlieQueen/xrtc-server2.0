#include "video/rtp_video_stream_receiver.h"

namespace xrtc {

namespace {

std::unique_ptr<RtpRtcpImpl> CreateRtpRtcpModule(
        const VideoReceiveStreamConfig& vconf,
        ReceiveStat* receive_stat)
{
    RtpRtcpConfig config;
    config.el = vconf.el;
    config.clock = vconf.clock;
    config.local_media_ssrc = vconf.rtp.local_ssrc;
    // 把 VideoReceiveStream 持有的 ReceiveStat 传给 RTCP 模块,
    // 使 RTCPSender 能拿到接收统计构建 RR 报告块
    config.receive_stat = receive_stat;

    auto rtp_rtcp = std::make_unique<RtpRtcpImpl>(config);
    rtp_rtcp->SetRtcpStatus(webrtc::RtcpMode::kCompound);
    return rtp_rtcp;
}

} // namespace

RtpVideoStreamReceiver::RtpVideoStreamReceiver(const VideoReceiveStreamConfig& config,
        ReceiveStat* rtp_receive_stat) :
    config_(config),
    rtp_receive_stat_(rtp_receive_stat),
    rtp_rtcp_(CreateRtpRtcpModule(config, rtp_receive_stat))
{
    // 把远端媒体流SSRC交给RTCP模块: RTCPReceiver解析SR时过滤用
    rtp_rtcp_->SetRemoteSsrc(config.rtp.remote_ssrc);
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

// 收到RTCP数据: 转给RTCP模块(RtpRtcpImpl), 由RTCPReceiver拆包解析
void RtpVideoStreamReceiver::DeliverRtcp(const uint8_t* data, size_t len) {
    rtp_rtcp_->IncomingRtcpPacket(data, len);
}

} // namespace xrtc
