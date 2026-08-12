#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTCP_RECEIVER_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTCP_RECEIVER_H_

#include <api/array_view.h>
#include <modules/rtp_rtcp/source/rtcp_packet/common_header.h>

#include "modules/rtp_rtcp/rtp_rtcp_config.h"

namespace xrtc {

// RTCP接收器: 接收并解析远端发来的RTCP复合包(后续课程按包类型分派处理)
class RTCPReceiver {
public:
    RTCPReceiver(const RtpRtcpConfig& config);
    ~RTCPReceiver();

    // 收到RTCP数据的入口(指针+长度形式), 内部转成ArrayView后统一处理
    void IncomingRtcpPacket(const uint8_t* packet, size_t packet_length);
    // 解析RTCP复合包: 拆包并按类型分派(当前为框架, 解析逻辑后续课程填充)
    void IncomingRtcpPacket(rtc::ArrayView<const uint8_t> packet);
    // 设置远端媒体流SSRC: HandleSr 解析SR时用它与 sr.sender_ssrc() 比对过滤
    void SetRemoteSsrc(uint32_t ssrc);
    // 查询最近一次SR包的信息, 供上层填RR报告块LSR/DLSR与算RTT:
    // received_ntp_* = SR包内NTP(发送端时钟, 填LSR), rtcp_arrival_* = SR到达时刻
    // (接收端本地时钟, 算DLSR), rtp_timestamp = SR包内RTP时间戳; 未收到过SR返回false
    bool NTP(uint32_t* received_ntp_secs,
             uint32_t* received_ntp_frac,
             uint32_t* rtcp_arrival_time_secs,
             uint32_t* rtcp_arrival_time_frac,
             uint32_t* rtp_timestamp);

private:
    struct PacketInformation;
    bool ParseCompoundPacket(rtc::ArrayView<const uint8_t> packet,
        PacketInformation *packet_information);

    void HandleSr(const webrtc::rtcp::CommonHeader& rtcp_block,
            PacketInformation* packet_information);
    void HandleRr(const webrtc::rtcp::CommonHeader& rtcp_block,
            PacketInformation* packet_information);

private:
    webrtc::Clock* clock_;  // 时间源: 记录SR到达时刻(CurrentNtpTime)用
    int num_skipped_packet_ = 0;
    // 远端(发送端)媒体流SSRC, 由 SetRemoteSsrc 设置, 用于过滤SR包
    uint32_t remote_ssrc_ = 0;
    // 最近一次SR包内的NTP时间戳(发送端时钟读数): 填RR报告块LSR用
    webrtc::NtpTime remote_sender_ntp_time_;
    // 最近一次SR包内的RTP时间戳(与NTP同刻): 时间对齐/RTT计算用
    uint32_t remote_sender_rtp_time_ = 0;
    // 最近一次SR的到达时刻(接收端本地时钟): 算DLSR的基准
    webrtc::NtpTime last_received_sr_ntp_;
    // 最近一次SR包内的累计发送包数/字节数: 将来做发送端统计用
    uint32_t remote_sender_packet_count_ = 0;
    uint32_t remote_sender_octet_count_ = 0;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTCP_RECEIVER_H_