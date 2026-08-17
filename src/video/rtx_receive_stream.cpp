#include "video/rtx_receive_stream.h"

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

}


} // namespace xrtc
