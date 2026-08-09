#include "video/rtp_video_stream_receiver.h"

namespace xrtc {

RtpVideoStreamReceiver::RtpVideoStreamReceiver(const VideoReceiveStreamConfig& config) :
    config_(config)
{

}

RtpVideoStreamReceiver::~RtpVideoStreamReceiver() {

}

} // namespace xrtc
