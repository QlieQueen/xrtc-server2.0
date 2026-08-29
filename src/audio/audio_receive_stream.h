#ifndef XRTCSERVER_AUDIO_RECEIVE_STREAM_H_
#define XRTCSERVER_AUDIO_RECEIVE_STREAM_H_

#include <memory>

#include "audio/audio_receive_stream_config.h"
#include "audio/channel_receive.h"

namespace xrtc {

class AudioReceiveStream {
public:
    AudioReceiveStream(const AudioReceiveStreamConfig& config);
    ~AudioReceiveStream();

    void OnRtpPacket(const webrtc::RtpPacketReceived& packet);

private:
    AudioReceiveStreamConfig config_;
    std::unique_ptr<ChannelReceive> channel_receive_;
};

} // namespace xrtc

#endif // XRTCSERVER_AUDIO_RECEIVE_STREAM_H_
