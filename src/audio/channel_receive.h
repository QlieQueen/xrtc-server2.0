#ifndef XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_
#define XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_

#include <modules/rtp_rtcp/source/rtp_packet_received.h>

#include "audio/audio_receive_stream_config.h"
#include "modules/rtp_rtcp/rtp_rtcp_config.h"
#include "modules/rtp_rtcp/rtp_rtcp_impl.h"

namespace xrtc {

class ChannelReceive {
public:
    ChannelReceive(const AudioReceiveStreamConfig& config,
            ReceiveStat* rtp_receive_stat);
    ~ChannelReceive();

    void OnRtpPacket(const webrtc::RtpPacketReceived& packet);
    void DeliverRtcp(const uint8_t* data, size_t len);

private:
    AudioReceiveStreamConfig config_;
    ReceiveStat* rtp_receive_stat_;
    std::unique_ptr<RtpRtcpImpl> rtp_rtcp_;
};

} // namespace xrtc

#endif // XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_
