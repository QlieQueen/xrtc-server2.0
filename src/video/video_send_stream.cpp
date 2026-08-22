#include "video/video_send_stream.h"

namespace xrtc {

VideoSendStream::VideoSendStream(const VideoSendStreamConfig& config) :
    config_(config)
{
}

VideoSendStream::~VideoSendStream() {

}

} // namespace xrtc
