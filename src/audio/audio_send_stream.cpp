#include "audio/audio_send_stream.h"

namespace xrtc {

AudioSendStream::AudioSendStream(const AudioSendStreamConfig& config) :
    config_(config)
{
}

AudioSendStream::~AudioSendStream() {

}

void AudioSendStream::UpdateRtpStat(int64_t now_ms, const webrtc::RtpPacketToSend& packet) {

}

} // namespace 
