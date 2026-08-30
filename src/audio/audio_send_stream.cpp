#include "audio/audio_send_stream.h"

namespace xrtc {

AudioSendStream::AudioSendStream(const AudioSendStreamConfig& config) :
    config_(config)
{
}

AudioSendStream::~AudioSendStream() {

}

} // namespace 
