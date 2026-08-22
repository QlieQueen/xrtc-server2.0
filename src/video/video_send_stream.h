#ifndef __XRTCSERVER_VIDEO_VIDEO_SEND_STREAM_H_
#define __XRTCSERVER_VIDEO_VIDEO_SEND_STREAM_H_

#include "video/video_send_stream_config.h"

namespace xrtc {

class VideoSendStream {
public:
    VideoSendStream(const VideoSendStreamConfig& config);
    ~VideoSendStream();

private:
    VideoSendStreamConfig config_;
};


} // namespace xrtc

#endif // __XRTCSERVER_VIDEO_VIDEO_SEND_STREAM_H_
