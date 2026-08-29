#include "audio/audio_receive_stream.h"

namespace xrtc {

AudioReceiveStream::AudioReceiveStream(const AudioReceiveStreamConfig& config) :
    config_(config),
    rtp_receive_stat_(ReceiveStat::Create(config.clock)),
    channel_receive_(std::make_unique<ChannelReceive>(config, rtp_receive_stat_.get()))
{

}

AudioReceiveStream::~AudioReceiveStream() {

}

void AudioReceiveStream::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    channel_receive_->OnRtpPacket(packet);
}

void AudioReceiveStream::DeliverRtcp(const uint8_t* data, size_t len) {
    channel_receive_->DeliverRtcp(data, len);    
}

} // namespace xrtc
