#ifndef XRTCSERVER_MODULES_VIDEO_CODING_RTP_FRAME_OBJECT_H_
#define XRTCSERVER_MODULES_VIDEO_CODING_RTP_FRAME_OBJECT_H_

#include <modules/rtp_rtcp/source/rtp_video_header.h>

namespace xrtc {

// 完整帧的元信息对象: 5.5 组帧时由"包集合"切成一帧一帧, 每帧一个 RtpFrameObject
// 只带帧的"户口信息"(序号范围+编码+元数据), 不带 payload —— SFU 不拼帧不解码
// 沿回调链(OnAssembledFrame → observer OnFrame → PC)通知"有一帧完整了"
class RtpFrameObject {
public:
    // first_seq_num / last_seq_num: 帧内首尾 RTP 包的序号(定义帧边界)
    // codec_type: 编码类型(H264); video_header: 帧元数据(frame_type/宽高/nalus)
    RtpFrameObject(uint16_t first_seq_num,
        uint16_t last_seq_num,
        webrtc::VideoCodecType codec_type,
        const webrtc::RTPVideoHeader& video_header);
    ~RtpFrameObject();

    uint16_t first_seq_num() const { return first_seq_num_; }
    uint16_t last_seq_num() const { return last_seq_num_; }
    webrtc::VideoCodecType codec_type() const { return codec_type_; }
    // 帧类型(关键帧/非关键帧): 透传 video_header, 下游(如 8.x PLI)靠它判断
    webrtc::VideoFrameType frame_type() const {
        return video_header_.frame_type;
    }

private:
    uint16_t first_seq_num_;                    // 帧内首包 seq
    uint16_t last_seq_num_;                     // 帧内末包 seq
    webrtc::VideoCodecType codec_type_;         // 编码类型
    webrtc::RTPVideoHeader video_header_;       // 帧元数据(拷贝)
};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_VIDEO_CODING_RTP_FRAME_OBJECT_H_