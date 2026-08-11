#ifndef XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_CONFIG_H_
#define XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_CONFIG_H_

#include <system_wrappers/include/clock.h>

#include "base/event_loop.h"

namespace xrtc {

// 视频接收流配置: 由上层(后续课程的PushStream)创建并传入
class VideoReceiveStreamConfig {
public:
    // 事件循环: 接收统计定时器、RTCP RR/SR 定时上报后续挂在它上面
    EventLoop* el = nullptr;
    // 时间源: 接收时间戳/统计计算(到达时间、jitter)用
    webrtc::Clock* clock = nullptr;

    struct Rtp {
        uint32_t local_ssrc = 0;
    } rtp;
};

} // namespce xrtc

#endif // XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_CONFIG_H_