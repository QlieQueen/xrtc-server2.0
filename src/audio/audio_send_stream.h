#ifndef XRTCSERVER_AUDIO_SEND_STREAM_H_
#define XRTCSERVER_AUDIO_SEND_STREAM_H_

#include <memory>

#include <modules/rtp_rtcp/source/rtp_packet_to_send.h>

#include "audio/audio_send_stream_config.h"
#include "modules/rtp_rtcp/rtp_rtcp_impl.h"

namespace xrtc {

class AudioSendStream {
public:
    AudioSendStream(const AudioSendStreamConfig& config);
    ~AudioSendStream();
    void UpdateRtpStat(int64_t now_ms, const webrtc::RtpPacketToSend& packet);
    void SetSrInfo(uint32_t rtp_timestamp, webrtc::NtpTime ntp);

private:
    AudioSendStreamConfig config_;
    std::unique_ptr<RtpRtcpImpl> rtp_rtcp_;
};

} // namespace xrtc

#endif // XRTCSERVER_AUDIO_SEND_STREAM_H_
