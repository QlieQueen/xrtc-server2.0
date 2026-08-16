#ifndef XRTCSERVER_VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_
#define XRTCSERVER_VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_

#include <modules/rtp_rtcp/source/rtp_packet_received.h>
#include <modules/rtp_rtcp/source/video_rtp_depacketizer_h264.h>
#include <modules/video_coding/packet_buffer.h>
#include <rtc_base/third_party/sigslot/sigslot.h>

#include "video/video_receive_stream_config.h"
#include "modules/rtp_rtcp/receive_stat.h"
#include "modules/rtp_rtcp/rtp_rtcp_impl.h"
#include "modules/video_coding/rtp_frame_object.h"
#include "modules/video_coding/nack_requester.h"

namespace xrtc {

// 视频RTP包接收器: 对应WebRTC VideoReceiveStream内部的同名类,
// 负责接收RTP包并维护接收统计(序列号回绕/丢包/jitter), 触发RTCP RR
// 当前为骨架, 接收逻辑后续课程填充
class RtpVideoStreamReceiver : public sigslot::has_slots<> {
public:
    // config: 接收流配置 (含 clock)
    // rtp_receive_stat: 借用指针, 所有权在 VideoReceiveStream, 这里只负责往里喂包
    RtpVideoStreamReceiver(const VideoReceiveStreamConfig& config,
        ReceiveStat* rtp_receive_stat);
    ~RtpVideoStreamReceiver();

    // 收到一个 RTP 包: 排除重传恢复的包后交给统计模块
    void OnRtpPacket(const webrtc::RtpPacketReceived& packet);
    // 收到RTCP数据: 转给RTCP模块(RtpRtcpImpl)解析
    void DeliverRtcp(const uint8_t* data, size_t len);

private:
    void ReceivePacket(const webrtc::RtpPacketReceived& packet);
    void OnReceivedPayloadData(
            rtc::CopyOnWriteBuffer codec_payload,
            const webrtc::RtpPacketReceived& packet,
            const webrtc::RTPVideoHeader& video_header);

    // 插包结果处理: packets 是完整帧的包集合(5.5 在这里切帧 → RtpFrameObject)
    void OnInsertedPacket(
            webrtc::video_coding::PacketBuffer::InsertResult result);
    void OnAssembledFrame(std::unique_ptr<RtpFrameObject> frame);
    void OnNackSend(const std::vector<uint16_t>& nack_list);

private:
    VideoReceiveStreamConfig config_;
    ReceiveStat* rtp_receive_stat_;   // 裸指针: 借用, 不负责释放
    std::unique_ptr<RtpRtcpImpl> rtp_rtcp_;
    std::unique_ptr<webrtc::VideoRtpDepacketizer> video_rtp_depacketizer_;
    // 乱序缓存 + 组帧器: 环形 vector, seq % size 映射槽位, 512 动态扩至 2048
    std::unique_ptr<webrtc::video_coding::PacketBuffer> packet_buffer_;
    std::unique_ptr<NackRequester> nack_module_;
};

} // namespace xrtc

#endif // XRTCSERVER_VIDEO_RTP_VIDEO_STREAM_RECEIVER_H_
