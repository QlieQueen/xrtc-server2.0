#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_

#include <map>
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
    // 设置是否为发送端(true生成SR, false生成RR)
    void SetSendingStatus(bool sending) { sending_ = sending; }

private:
    // 计算复合RTCP包: 将本次报文类型标记入集合, 并组装发送
    absl::optional<uint32_t> ComputeCompundRTCPPacket(
            webrtc::RTCPPacketType packet_type);
    // 标记某报文类型为待发送, is_volatile=true 表示一次性标记(发送后删除)
    void SetFlag(uint32_t type, bool is_volatile);
    // 检查某报文类型是否已在待发送集合中
    bool IsFlagPresent(uint32_t type);
    // 按发送模式决定是否生成SR/RR统计报告并加入集合
    void PrepareReport();
    // 消费标记: volatile标记消费后删除, 非volatile标记force=true时才删除
    bool ConsumeFlag(uint32_t type, bool force = false);

    void BuildRR();

private:
    webrtc::Clock* clock_;
    // 当前RTCP发送模式, 默认关闭
    webrtc::RtcpMode method_ = webrtc::RtcpMode::kOff;
    // 是否处于发送状态(决定报告生成SR还是RR), 默认接收端
    bool sending_ = false;

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

    // 构建函数指针类型: 指向RTCPSender成员函数, 无参数无返回值
    typedef void (RTCPSender::*BuilderFunc)();
    // 报文类型(type) -> 构建函数 映射表, 在构造函数中注册
    std::map<uint32_t, BuilderFunc> builders_;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_SENDER_H_
