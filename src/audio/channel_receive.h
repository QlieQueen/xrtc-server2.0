#ifndef XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_
#define XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_

#include <modules/rtp_rtcp/source/rtp_packet_received.h>

#include "audio/audio_receive_stream_config.h"
#include "modules/rtp_rtcp/rtp_rtcp_config.h"

namespace xrtc {

class ChannelReceive {
public:
    ChannelReceive(const AudioReceiveStreamConfig& config);
    ~ChannelReceive();

    void OnRtpPacket(const webrtc::RtpPacketReceived& packet);

private:
    AudioReceiveStreamConfig config_;
};

} // namespace xrtc

#endif // XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_
