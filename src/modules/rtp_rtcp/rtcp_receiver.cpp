#include "modules/rtp_rtcp/rtcp_receiver.h"

#include <rtc_base/logging.h>
#include <modules/rtp_rtcp/source/rtcp_packet/sender_report.h>
#include <modules/rtp_rtcp/source/rtcp_packet/receiver_report.h>

namespace xrtc {

struct RTCPReceiver::PacketInformation {

};

RTCPReceiver::RTCPReceiver(const RtpRtcpConfig& config) :
    clock_(config.clock)
{

}

RTCPReceiver::~RTCPReceiver() {

}

// 设置远端媒体流SSRC: HandleSr 过滤SR包用, 只处理本端关注的那路流
void RTCPReceiver::SetRemoteSsrc(uint32_t ssrc) {
    remote_ssrc_ = ssrc;
}

// 查询最近一次SR包的信息, 由HandleSr记录的成员填充:
// 供上层填RR报告块LSR(发送端NTP)/DLSR(本地到达时刻)与算RTT(RTP时间戳对齐)
bool RTCPReceiver::NTP(uint32_t* received_ntp_secs,
        uint32_t* received_ntp_frac,
        uint32_t* rtcp_arrival_time_secs,
        uint32_t* rtcp_arrival_time_frac,
        uint32_t* rtp_timestamp)
{
    if (!last_received_sr_ntp_.Valid()) { // 此时还没有收到任何SR包
        return false;
    }

    // SR包中的NTP时间秒数部分
    if (received_ntp_secs) {
        *received_ntp_secs = remote_sender_ntp_time_.seconds();
    }

    // SR包中的NTP时间秒数以下部分
    if (received_ntp_frac) {
        *received_ntp_frac = remote_sender_ntp_time_.fractions();
    }

    // SR包到达时的本地NTP时间
    if (rtcp_arrival_time_secs) {
        *rtcp_arrival_time_secs = last_received_sr_ntp_.seconds();
    }

    if (rtcp_arrival_time_frac) {
        *rtcp_arrival_time_frac = last_received_sr_ntp_.fractions();
    }

    // SR包中的RTP timestamp
    if (rtp_timestamp) {
        *rtp_timestamp = remote_sender_rtp_time_;
    }

    return true;
}

// 指针形式入口: 包装成ArrayView后交给统一解析入口
void RTCPReceiver::IncomingRtcpPacket(const uint8_t* packet, size_t packet_length) {
    IncomingRtcpPacket(rtc::MakeArrayView<const uint8_t>(packet, packet_length));
}

// 统一解析入口: 当前为框架空实现, 后续课程实现复合包拆包与SR/RR等类型分派
void RTCPReceiver::IncomingRtcpPacket(rtc::ArrayView<const uint8_t> packet) {
    if (packet.empty()) {
        RTC_LOG(LS_WARNING) << "incoming rtcp packet is empty";
        return;
    }

    PacketInformation packet_information;
    if (!ParseCompoundPacket(packet, &packet_information)) {
        return;
    }

}


//    0                   1           1       2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 0 |V=2|P|   C/F   |  Packet Type  |             length            |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                                                               |
//   |                       ......                                  |
//   |                                                               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// N |V=2|P|   C/F   |  Packet Type  |             length            |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                                                               |
//   |                       ......                                  |
//   |                                                               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
bool RTCPReceiver::ParseCompoundPacket(rtc::ArrayView<const uint8_t> packet,
        PacketInformation* packet_information)
{
    // 复合包由多个RTCP块依次拼接: 每个块 = 4字节公共头 + payload
    // 循环靠 NextPacket() 推进, 它返回"当前块结束位置 = 下一块起点",
    // 且仅在 Parse 成功后可信(失败时 payload_ 可能为 nullptr/残留值)
    webrtc::rtcp::CommonHeader rtcp_block;
    for (const uint8_t* next_block = packet.begin(); next_block != packet.end();
            next_block = rtcp_block.NextPacket())
    {
        ptrdiff_t remaining_block_size = packet.end() - next_block;
        // Parse 成功返回 true; payload_ 已跳过4字节头, 故 NextPacket() 无需再加头部长度
        if (!rtcp_block.Parse(next_block, remaining_block_size)) {
            if (next_block == packet.begin()) {
                // 首块非法 -> 整个复合包不可信, 直接丢弃
                RTC_LOG(LS_WARNING) << "invalid incoming rtcp packet";
                return false;
            }

            // 中间块非法(截断/脏数据): 保留已解析部分, 计数后停止解析
            // 必须 break, 否则 NextPacket() 基于不可信状态推进可能越界/死循环
            ++num_skipped_packet_;
            break;
        }

        switch (rtcp_block.type()) {
            case webrtc::rtcp::SenderReport::kPacketType:
                HandleSr(rtcp_block, packet_information);
                break;
            case webrtc::rtcp::ReceiverReport::kPacketType:
                HandleRr(rtcp_block, packet_information);
                break;
            default:
                RTC_LOG(LS_WARNING) << "unknown rtcp packet_type: " << rtcp_block.type();
                ++num_skipped_packet_;
                break;
        }
    }

    return true;
}

void RTCPReceiver::HandleSr(const webrtc::rtcp::CommonHeader& rtcp_block,
        PacketInformation* packet_information)
{
    // 解析SR包体(公共头已由 ParseCompoundPacket 解析校验)
    webrtc::rtcp::SenderReport sr;
    if (!sr.Parse(rtcp_block)) {
        ++num_skipped_packet_;
        return;
    }

    // SSRC过滤: SR的sender ssrc = 推流端媒体流SSRC, 只处理本端关注的那路流,
    // 避免多流/噪声流刷屏
    uint32_t remote_ssrc = sr.sender_ssrc();
    if (remote_ssrc == remote_ssrc_) {
        RTC_LOG(LS_WARNING) << "==========sr ssrc: " << sr.sender_ssrc()
            << ", packet_count: " << sr.sender_packet_count();
        // 记录SR信息(供NTP()查询): SR内NTP/RTP时间戳=发送端时钟(填LSR/算RTT),
        // 到达时刻=接收端本地时钟(算DLSR), 包/字节计数=发送端累计统计
        remote_sender_ntp_time_ = sr.ntp();
        remote_sender_rtp_time_ = sr.rtp_timestamp();
        last_received_sr_ntp_ = clock_->CurrentNtpTime();
        remote_sender_packet_count_ = sr.sender_packet_count();
        remote_sender_octet_count_ = sr.sender_octet_count();
    }
}

void RTCPReceiver::HandleRr(const webrtc::rtcp::CommonHeader& rtcp_block,
        PacketInformation* packet_information)
{

}


} // namespace xrtc
