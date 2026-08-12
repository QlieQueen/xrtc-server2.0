#ifndef XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_H_
#define XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_H_

#include "video/video_receive_stream_config.h"
#include "video/rtp_video_stream_receiver.h"
#include "modules/rtp_rtcp/receive_stat.h"

namespace xrtc {

// 视频接收流: 代表"接收一路视频", 持有RTP包接收器,
// 并拥有接收统计模块(4.4), 后续课程在此添加 RTCP RR/SR 生成解析
class VideoReceiveStream {
public:
    VideoReceiveStream(const VideoReceiveStreamConfig& config);
    ~VideoReceiveStream();

    void OnRtpPacket(const webrtc::RtpPacketReceived& packet);
    // 收到RTCP数据: 转给内部接收器, 最终由RTCP模块解析
    void DeliverRtcp(const uint8_t* data, size_t len);

private:
    VideoReceiveStreamConfig config_;
    // 接收统计: 唯一拥有者, 先于接收器创建, 通过 get() 把裸指针借给接收器
    std::unique_ptr<ReceiveStat> rtp_receive_stat_;
    // 接收器成员, 内部持有指向上面统计模块的借用指针
    RtpVideoStreamReceiver rtp_video_stream_receiver_;
};

} // namespace xrtc

#endif // XRTCSERVER_VIDEO_VIDEO_RECEIVE_STREAM_H_
