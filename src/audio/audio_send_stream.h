#ifndef XRTCSERVER_AUDIO_SEND_STREAM_H_
#define XRTCSERVER_AUDIO_SEND_STREAM_H_

#include <modules/rtp_rtcp/source/rtp_packet_to_send.h>

#include "audio/audio_send_stream_config.h"

namespace xrtc {

class AudioSendStream {
public:
    AudioSendStream(const AudioSendStreamConfig& config);
    ~AudioSendStream();
    void UpdateRtpStat(int64_t now_ms, const webrtc::RtpPacketToSend& packet);

private:
    AudioSendStreamConfig config_;
};

} // namespace xrtc

#endif // XRTCSERVER_AUDIO_SEND_STREAM_H_
