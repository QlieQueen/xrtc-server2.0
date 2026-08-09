#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTCP_RECEIVER_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTCP_RECEIVER_H_

#include <api/array_view.h>

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

private:
    struct PacketInformation;
    bool ParseCompoundPacket(rtc::ArrayView<const uint8_t> packet,
        PacketInformation *packet_information);

private:
    int num_skipped_packet_ = 0;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTCP_RECEIVER_H_