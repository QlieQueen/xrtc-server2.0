#include "modules/rtp_rtcp/rtcp_sender.h"

#include <rtc_base/logging.h>

namespace xrtc {

RTCPSender::RTCPSender(const RtpRtcpConfig& config) :
    clock_(config.clock)
{
    // 注册RTCP报文类型对应的构建函数: RR(接收端报告)由BuildRR构建,
    // SR(发送端报告)等其他类型的构建函数在后续课程注册
    builders_[webrtc::kRtcpRr] = &RTCPSender::BuildRR;
}

RTCPSender::~RTCPSender() {

}

// 设置RTCP发送模式: kOff 关闭 / kCompound 复合包 / kNonCompound 单个包
void RTCPSender::SetRtcpStatus(webrtc::RtcpMode method) {
    method_ = method;
}

// 把某个 RTCP 报文类型标记为待发送, is_volatile=true 表示一次性标记(发送完就删除)
void RTCPSender::SetFlag(uint32_t type, bool is_volatile) {
    report_flags_.insert(ReportFlag(type, is_volatile));
}

// 检查某个 RTCP 报文类型是否已在待发送集合中(查找只看type, 与is_volatile无关)
bool RTCPSender::IsFlagPresent(uint32_t type) {
    return report_flags_.find(ReportFlag(type, false)) != report_flags_.end();
}

// 消费标记: 返回集合中是否存在该报文类型;
// volatile标记消费后删除(只发送一次), 非volatile标记只有force=true时才删除(常驻周期发送)
bool RTCPSender::ConsumeFlag(uint32_t type, bool force) {
    auto it = report_flags_.find(ReportFlag(type, false));
    if (it == report_flags_.end()) {
        return false;
    }

    if (it->is_volatile || force) {
        report_flags_.erase(it);
    }

    return true;
}

// 发送RTCP报文: 入口函数, 根据报文类型计算并发送复合RTCP包
void RTCPSender::SendRTCP(webrtc::RTCPPacketType packet_type) {
    // 返回值表示本次复合包的字节数, 当前课程尚未使用, 后续发送时用到
    auto result = ComputeCompundRTCPPacket(packet_type);
}

// 计算复合RTCP包: 先把本次要发送的报文类型记入 report_flags_,
// 后续再遍历集合逐项组装报文并发送(集合中非volatile的项会常驻, 周期性发送)
absl::optional<uint32_t> RTCPSender::ComputeCompundRTCPPacket(
        webrtc::RTCPPacketType packet_type)
{
    // RTCP被关闭时直接返回, 不发送任何报文
    if (method_ == webrtc::RtcpMode::kOff) {
        RTC_LOG(LS_WARNING) << "cannot send rtcp if it is disabled";
        return -1;
    }

    // 标记为volatile, 表示本次发送的报文类型只在集合中保留一次
    SetFlag(packet_type, true);

    // 按发送模式决定是否附带SR/RR统计报告(compound/reduced-size规则)
    PrepareReport();

    // 遍历report flags, 生成相应类型的rtcp包
    auto it = report_flags_.begin();
    while (it != report_flags_.end()) {
        // 先保存当前元素的type, 再清理迭代器, 避免擦除后it指向下一个元素导致取错type
        uint32_t rtcp_packet_type = it->type;
        if (it->is_volatile) {
            report_flags_.erase(it++);
        } else {
            ++it;
        }

        // 用保存的type查找对应的builder并构建报文
        auto builder_it = builders_.find(rtcp_packet_type);
        if (builder_it == builders_.end()) {
            RTC_LOG(LS_WARNING) << "could not build rtcp packet for packet type: "
                << rtcp_packet_type;
        } else {
            BuilderFunc func = builder_it->second;
            (this->*func)();
        }
    }

    return absl::nullopt;
}

// 决定是否生成SR(发送端)/RR(接收端)统计报告并加入待发送集合
void RTCPSender::PrepareReport() {
    bool generate_report;
    // 集合中已有SR或RR标记(本次或常驻请求), 无需重复设置
    if (IsFlagPresent(webrtc::kRtcpSr) || IsFlagPresent(webrtc::kRtcpRr)) {
        generate_report = true;
    } else {
        // kReducedSize模式: 仅在收到kRtcpReport请求时消费一次标记, 生成一次报告;
        // kCompound模式: 复合包总是携带报告
        generate_report = ((method_ == webrtc::RtcpMode::kReducedSize &&
            ConsumeFlag(webrtc::kRtcpReport)) ||
            (method_ == webrtc::RtcpMode::kCompound));

        // 发送端生成SR, 接收端生成RR, 均标记为volatile(本次发完即删)
        if (generate_report) {
            SetFlag(sending_ ? webrtc::kRtcpSr : webrtc::kRtcpRr, true);
        }
    }
}

// 构建RR(接收端统计报告)报文: 负责把接收统计信息打包成RTCP RR包,
// 具体打包逻辑(填充接收端数据)在后续课程实现
void RTCPSender::BuildRR() {

}

}
