#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_

#include <set>
#include <modules/rtp_rtcp/include/rtp_rtcp_defines.h>

#include "modules/rtp_rtcp/rtp_rtcp_config.h"

namespace xrtc {

// RTCP发送器: 负责RTCP报文的发送(复合包计算、待发送报文类型管理)
class RTCPSender {
public:
    RTCPSender(const RtpRtcpConfig& config);
    ~RTCPSender();

    // 发送指定类型的RTCP报文
    void SendRTCP(webrtc::RTCPPacketType packet_type);
    // 设置RTCP发送模式(kOff/kCompound/kNonCompound)
    void SetRtcpStatus(webrtc::RtcpMode method);

private:
    // 计算复合RTCP包: 将本次报文类型标记入集合, 并组装发送
    absl::optional<uint32_t> ComputeCompundRTCPPacket(
            webrtc::RTCPPacketType packet_type);
    // 标记某报文类型为待发送, is_volatile=true 表示一次性标记(发送后删除)
    void SetFlag(uint32_t type, bool is_volatile);

private:
    webrtc::Clock* clock_;
    // 当前RTCP发送模式, 默认关闭
    webrtc::RtcpMode method_ = webrtc::RtcpMode::kOff;

    // 待发送的RTCP报文类型标记
    // type: 报文类型(对应RTCPPacketType)
    // is_volatile: 是否为一次性标记(true: 发送完删除; false: 常驻集合, 周期发送)
    struct ReportFlag {
        ReportFlag(uint32_t type, bool is_volatile) :
            type(type), is_volatile(is_volatile) {}

        bool operator<(const ReportFlag& flag) const {
            return type < flag.type;
        }

        bool operator==(const ReportFlag& flag) const {
            return type == flag.type;
        }

        uint32_t type;
        bool is_volatile;
    };

    // 待发送的RTCP报文类型集合(按type排序), 复合包按集合逐项组装
    std::set<ReportFlag> report_flags_;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_
