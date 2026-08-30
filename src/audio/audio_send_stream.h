#ifndef XRTCSERVER_AUDIO_SEND_STREAM_H_
#define XRTCSERVER_AUDIO_SEND_STREAM_H_

#include "audio/audio_send_stream_config.h"

namespace xrtc {

class AudioSendStream {
public:
    AudioSendStream(const AudioSendStreamConfig& config);
    ~AudioSendStream();

private:
    AudioSendStreamConfig config_;
};

} // namespace xrtc

#endif // XRTCSERVER_AUDIO_SEND_STREAM_H_
