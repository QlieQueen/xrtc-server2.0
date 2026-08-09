#include "modules/rtp_rtcp/rtcp_receiver.h"

#include <rtc_base/logging.h>
#include <modules/rtp_rtcp/source/rtcp_packet/common_header.h>

namespace xrtc {

struct RTCPReceiver::PacketInformation {

};

RTCPReceiver::RTCPReceiver(const RtpRtcpConfig& config) {

}

RTCPReceiver::~RTCPReceiver() {

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
    }

    return true;
}


} // namespace xrtc
