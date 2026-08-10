#include "modules/rtp_rtcp/receive_stat.h"

#include <rtc_base/logging.h>

namespace xrtc {


StreamStat::StreamStat(uint32_t ssrc, webrtc::Clock* clock) :
    ssrc_(ssrc),
    clock_(clock)
{

}

StreamStat::~StreamStat() {

}

void StreamStat::UpdateCounters(const webrtc::RtpPacketReceived& packet) {
    int64_t now_ms = clock_->TimeInMilliseconds();
    receive_counters_.transmitted.AddPacket(packet);
    // 账本模型: 每包到货先还 1 笔"丢失账"(-1);
    // 顺序包再补记自己与前沿之间缺的包 (+(seq - max)),
    // 乱序包靠 return 短路, 只撤销自己当初那笔账
    --cumulative_loss_;

    int64_t sequence_number = seq_unwrapper_.UnwrapWithoutUpdate(packet.SequenceNumber());
    // 收到第一个包
    if (!ReceivedRtpPacket()) {
        received_seq_first_ = sequence_number;
        received_seq_max_ = sequence_number - 1;
    } else if (UpdateOutOfOrder(packet, sequence_number, now_ms)) { // 发生乱序
        return;
    }

    // 顺序到达的rtp包
    cumulative_loss_ += (sequence_number - received_seq_max_);
    received_seq_max_ = sequence_number;

    // 乱序包 return 前不提交: 解绕器状态只跟随顺序前沿, 与 received_seq_max_ 保持同步
    seq_unwrapper_.UpdateLast(sequence_number);

}

// 乱序/序列突变判定: 4.6 骨架恒 true (所有非首包走乱序分支, 统计值不完整),
// 4.7 按阈值 450 实现: 比前沿旧 → 乱序/重传; 比前沿大太多 → 序列突变
bool StreamStat::UpdateOutOfOrder(const webrtc::RtpPacketReceived& packet,
    int64_t sequence_number,
    int64_t now_ms)
{
    (void)packet;
    (void)sequence_number;
    (void)now_ms;
    // 表示乱序
    return true;
}


ReceiveStat::ReceiveStat(webrtc::Clock* clock) :
    clock_(clock)
{

}

ReceiveStat::~ReceiveStat() {

}

// 工厂方法: 统一入口创建统计模块
std::unique_ptr<ReceiveStat> ReceiveStat::Create(webrtc::Clock* clock) {
    return std::make_unique<ReceiveStat>(clock);
}

// 取引用是关键: operator[] 对不存在的 key 会插入 value-initialized 值
// (unique_ptr → nullptr) 并返回其引用, 引用赋值即写回容器本身;
// 若用拷贝则创建只改局部, 容器永远为空
StreamStat* ReceiveStat::GetOrCreateStat(uint32_t ssrc) {
    std::unique_ptr<StreamStat>& stat = stats_[ssrc];
    if (nullptr == stat) {
        stat = std::make_unique<StreamStat>(ssrc, clock_);
    }

    return stat.get();
}

// 每个 RTP 包到达时调用, 按包里带的 ssrc 分流到对应统计桶,
// 4.4 的链路验证日志已注释, 统计逻辑从 4.6 起在 UpdateCounters 里填充
void ReceiveStat::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    //RTC_LOG(LS_WARNING) << "====================ssrc: " << packet.Ssrc()
    //    << ", sequence_number: " << packet.SequenceNumber();

    GetOrCreateStat(packet.Ssrc())->UpdateCounters(packet);
}


} // namespace xrtc 
