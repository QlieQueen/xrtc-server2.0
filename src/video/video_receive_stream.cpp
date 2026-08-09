#include "video/video_receive_stream.h"

namespace xrtc {

VideoReceiveStream::VideoReceiveStream(const VideoReceiveStreamConfig& config) :
    config_(config),
    rtp_video_stream_receiver_(config)
{
}


VideoReceiveStream::~VideoReceiveStream() {
}

} // namespace xrtc
