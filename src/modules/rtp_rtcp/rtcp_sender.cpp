#include "modules/rtp_rtcp/rtcp_sender.h"

#include <rtc_base/logging.h>
#include <modules/rtp_rtcp/source/rtcp_packet/receiver_report.h>
#include <modules/rtp_rtcp/source/rtp_rtcp_config.h>
#include <modules/rtp_rtcp/source/time_util.h>

namespace xrtc {

// PacketSender: 复合RTCP包打包器, 负责把多个RTCP包序列化进缓冲区, 攒齐后一次性发出
class RTCPSender::PacketSender {
public:
    PacketSender(size_t max_packet_size,
            webrtc::rtcp::RtcpPacket::PacketReadyCallback callback) :
        max_packet_size_(max_packet_size),
        callback_(callback) {}
    ~PacketSender() {};

    // 把单个RTCP包追加到复合包: 包对象通过自己的Create虚函数序列化进buffer;
    // 若剩余空间不足, Create内部会先回调发走已填部分(index_归零), 复用buffer继续写
    void AppendPacket(const webrtc::rtcp::RtcpPacket& packet) {
        packet.Create(buffer_, &index_, max_packet_size_, callback_);
    }

    // 把缓冲区中已复合的报文一次性交给回调(对应一个UDP报文), 然后清空游标
    void Send() {
        if (index_ > 0) {
            callback_(rtc::ArrayView<const uint8_t>(buffer_, index_));
            index_ = 0;
        }
    }

private:
    size_t max_packet_size_; // 真实的大小
    // 发送回调: 收到一个完整的RTCP报文, 由上层真正发送(当前课程仅打日志, 后续换网络发送)
    webrtc::rtcp::RtcpPacket::PacketReadyCallback callback_;
    size_t index_ = 0;       // 缓冲区的写入游标(已写入字节数)
    uint8_t buffer_[IP_PACKET_SIZE]; // 预留的空间
};


RTCPSender::RTCPSender(const RtpRtcpConfig& config) :
    clock_(config.clock),
    ssrc_(config.local_media_ssrc),
    receive_stat_(config.receive_stat), // 4.10 接入接收统计, RR 报告块的数据源
    max_packet_size_(IP_PACKET_SIZE - 28) // 去掉IP头部和UDP头部
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
int RTCPSender::SendRTCP(webrtc::RTCPPacketType packet_type) {
    // 发送回调: 每收到一个完整的RTCP报文(UDP载荷)触发一次;
    // 当前课程仅打印包大小验证链路, 后续课程会替换为真正的网络发送
    auto callback = [&](rtc::ArrayView<const uint8_t> packet) {
        RTC_LOG(LS_WARNING) << "====================build rtcp packet, size: " << packet.size();
    };

    // PacketSender无默认构造, 用optional延迟构造(拿到max_packet_size_和回调后再创建)
    absl::optional<PacketSender> sender;
    sender.emplace(max_packet_size_, callback);

    // 计算并组装复合包; result有值表示出错(如RTCP未开启返回-1), 直接返回错误码
    auto result = ComputeCompundRTCPPacket(packet_type, *sender);
    if (result) {
        return *result;
    }

    // 复合包组装完成, 把缓冲区中的报文一次性发出
    sender->Send();
    return 0;
}

// 计算复合RTCP包: 先把本次要发送的报文类型记入 report_flags_,
// 后续再遍历集合逐项组装报文并发送(集合中非volatile的项会常驻, 周期性发送)
absl::optional<uint32_t> RTCPSender::ComputeCompundRTCPPacket(
        webrtc::RTCPPacketType packet_type,
        PacketSender& sender)
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
            (this->*func)(sender);
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

// 从接收统计取报告块: RTCP_MAX_REPORT_BLOCKS=31(一个 RR 包的块数上限);
// receive_stat_ 由 RtpVideoStreamReceiver 创建时接入(VideoReceiveStream 持有),
// RTP 包已喂入计数; 有报告块且记录过SR到达时间时, 再填充块内 LSR(抄发送端NTP) 与
// DLSR(收SR→发RR延迟, feedback_state 由 RTCPReceiver 解析 SR 后填充)
std::vector<webrtc::rtcp::ReportBlock> RTCPSender::CreateRtcpReportBlocks(
        const FeedbackState& feedback_state)
{
    std::vector<webrtc::rtcp::ReportBlock> result;
    if (!receive_stat_) {
        return result;
    }

    result = receive_stat_->RtcpReportBlocks(webrtc::RTCP_MAX_REPORT_BLOCKS);

    // 填充LSR和DLSR: 需要先收到过SR包(记录过到达时刻)
    if (!result.empty() && ((feedback_state.last_rr_ntp_secs > 0) ||
            (feedback_state.last_rr_ntp_frac > 0)))
    {
        // DLSR = 发RR时刻 - 收到最近SR时刻, 均为32位压缩NTP
        // (秒低16位 | 分数高16位, RFC3550 报告块字段格式)
        int32_t now = webrtc::CompactNtp(clock_->CurrentNtpTime());
        // 收到最近一次SR包时，接收端压缩后的32位NTP时间
        int32_t receive_time = feedback_state.last_rr_ntp_secs & 0x0000FFFF;
        receive_time <<= 16;
        receive_time += ((feedback_state.last_rr_ntp_frac & 0xFFFF0000) >> 16);
        int32_t delay_since_last_sr = now - receive_time;
        
        for (auto& report_block : result) {
            report_block.SetLastSr(feedback_state.remote_sr);
            report_block.SetDelayLastSr(delay_since_last_sr);
        }
    }

    return result;
}


// 构建RR(接收端统计报告)报文: 头部 SSRC 填本端(接收方)的 ssrc,
// 报告块从接收统计取(4.10 打通链路, 块内字段由 4.11 填实)
void RTCPSender::BuildRR(PacketSender& sender) {
    FeedbackState feedback_state;

    webrtc::rtcp::ReceiverReport rr;
    rr.SetSenderSsrc(ssrc_);
    rr.SetReportBlocks(CreateRtcpReportBlocks(feedback_state));
    // rr 追加进复合包(sender.AppendPacket)留到后续课程, 当前先消除未用参数警告
    (void)sender;
}

}
