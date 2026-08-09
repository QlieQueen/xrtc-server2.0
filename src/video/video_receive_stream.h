#ifndef XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_H_
#define XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_H_

#include "video/video_receive_stream_config.h"
#include "video/rtp_video_stream_receiver.h"

namespace xrtc {

// 视频接收流: 代表"接收一路视频", 持有RTP包接收器,
// 后续课程在此添加接收统计上报/RTCP RR/SR 生成解析
class VideoReceiveStream {
public:
    VideoReceiveStream(const VideoReceiveStreamConfig& config);
    ~VideoReceiveStream();

private:
    VideoReceiveStreamConfig config_;
    RtpVideoStreamReceiver rtp_video_stream_receiver_;
};

} // namespace xrtc

#endif // XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_H_
