#include "audio/audio_receive_stream.h"

namespace xrtc {

AudioReceiveStream::AudioReceiveStream(const AudioReceiveStreamConfig& config) :
    config_(config),
    channel_receive_(std::make_unique<ChannelReceive>(config))
{

}

AudioReceiveStream::~AudioReceiveStream() {

}

void AudioReceiveStream::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    channel_receive_->OnRtpPacket(packet);
}

} // namespace xrtc
