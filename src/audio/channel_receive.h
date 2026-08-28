#ifndef XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_
#define XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_

#include "audio/audio_receive_stream_config.h"

namespace xrtc {

class ChannelReceive {
public:
    ChannelReceive(const AudioReceiveStreamConfig& config);
    ~ChannelReceive();

private:
    AudioReceiveStreamConfig config_;
};

} // namespace xrtc

#endif // XRTCSERVER_AUDIO_CHANNEL_RECEIVE_H_
