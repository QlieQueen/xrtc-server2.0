#include "video/rtx_receive_stream.h"

#include <rtc_base/logging.h>

#include "video/rtp_video_stream_receiver.h"
#include "modules/rtp_rtcp/receive_stat.h"

namespace xrtc {

RtxReceiveStream::RtxReceiveStream(RtpVideoStreamReceiver* media_sink,
        uint32_t media_ssrc,
        const std::map<int, int> rtx_associated_payload_types,
        ReceiveStat* rtp_receive_stat) :
    media_sink_(media_sink),
    media_ssrc_(media_ssrc),
    rtx_associated_payload_types_(rtx_associated_payload_types),
    rtp_receive_stat_(rtp_receive_stat)
{

}

RtxReceiveStream::~RtxReceiveStream() {

}

void RtxReceiveStream::OnRtpPacket(const webrtc::RtpPacketReceived& rtx_packet) {
    if (rtp_receive_stat_) {
        rtp_receive_stat_->OnRtpPacket(rtx_packet);
    }

    rtc::ArrayView<const uint8_t> payload = rtx_packet.payload();
    if (payload.size() < webrtc::kRtxHeaderSize) {
        return;
    }

    auto it = rtx_associated_payload_types_.find(rtx_packet.PayloadType());
    if (it == rtx_associated_payload_types_.end()) { // 没有找到
        RTC_LOG(LS_WARNING) << "unknown payload type: "
            << static_cast<int>(rtx_packet.PayloadType())
            << " on ssrc " << rtx_packet.Ssrc();
        return;
    }

    webrtc::RtpPacketReceived media_packet;
    media_packet.CopyHeaderFrom(rtx_packet);
    media_packet.SetSsrc(media_ssrc_);
    media_packet.SetSequenceNumber((payload[0] << 8) + payload[1]);
    media_packet.SetPayloadType(it->second);
    media_packet.set_recovered(true);
    media_packet.set_arrival_time(rtx_packet.arrival_time());

    // 设置payload
    // 跳过rtx头部
    rtc::ArrayView<const uint8_t> rtx_payload = payload.subview(webrtc::kRtxHeaderSize);
    uint8_t* media_payload = media_packet.AllocatePayload(rtx_payload.size());
    memcpy(media_payload, rtx_payload.data(), rtx_payload.size());
    
    RTC_LOG(LS_WARNING) << "=============recover packet seq: " << media_packet.SequenceNumber();

    media_sink_->OnRtpPacket(media_packet);
}


} // namespace xrtc
