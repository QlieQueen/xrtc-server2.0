#include "video/video_send_stream.h"

#include <modules/rtp_rtcp/source/byte_io.h>

namespace xrtc {

VideoSendStream::VideoSendStream(const VideoSendStreamConfig& config) :
    config_(config)
{
    RtpRtcpConfig rr_config;
    rr_config.el = config.el;
    rr_config.clock = config.clock;
    rr_config.rtp_rtcp_module_observer = config.rtp_rtcp_module_observer;
    rr_config.local_media_ssrc = config.rtp.local_ssrc;
    rr_config.rtx_send_ssrc = config.rtp.local_rtx_ssrc;

    rtp_rtcp_ = std::make_unique<RtpRtcpImpl>(rr_config);
    rtp_rtcp_->SetRtcpStatus(webrtc::RtcpMode::kCompound);
    rtp_rtcp_->SetSendingStatus(true);
}

VideoSendStream::~VideoSendStream() {

}

void VideoSendStream::DeliverRtcp(const uint8_t* data, size_t len) {
    rtp_rtcp_->IncomingRtcpPacket(data, len);   
}

void VideoSendStream::UpdateRtpStat(int64_t now_ms, const webrtc::RtpPacketToSend& packet) {
    rtp_rtcp_->UpdateRtpStat(now_ms, packet);
}

void VideoSendStream::SetSrInfo(uint32_t rtp_timestamp, webrtc::NtpTime ntp) {
    rtp_rtcp_->SetSrInfo(rtp_timestamp, ntp);
}

std::unique_ptr<webrtc::RtpPacketToSend> VideoSendStream::BuildRtxPacket(
        const webrtc::RtpPacketToSend& packet)
{
    std::unique_ptr<webrtc::RtpPacketToSend> rtx_packet = 
        std::make_unique<webrtc::RtpPacketToSend>(nullptr);

    // 设置RTP头部
    rtx_packet->SetPayloadType(config_.rtp.rtx.payload_type);
    rtx_packet->SetSsrc(config_.rtp.local_rtx_ssrc);
    rtx_packet->SetMarker(packet.Marker());
    rtx_packet->SetTimestamp(packet.Timestamp());
    rtx_packet->SetSequenceNumber(rtx_seq_++);
    rtx_packet->SetCsrcs(packet.Csrcs());

    // 分配payload的内存
    uint8_t* rtx_payload = rtx_packet->AllocatePayload(
            packet.payload_size() + webrtc::kRtxHeaderSize);
    if (!rtx_payload) {
        return nullptr;
    }

    // add OSN
    webrtc::ByteWriter<uint16_t>::WriteBigEndian(rtx_payload, packet.SequenceNumber());

    // copy原始的负载
    auto payload = packet.payload();
    memcpy(rtx_payload + webrtc::kRtxHeaderSize, payload.data(), payload.size());

    // 添加其他的属性
    rtx_packet->set_additional_data(packet.additional_data());
    rtx_packet->set_capture_time_ms(packet.capture_time_ms());

    rtx_packet->set_retransmitted_sequence_number(packet.SequenceNumber());

    return rtx_packet;
}

} // namespace xrtc
