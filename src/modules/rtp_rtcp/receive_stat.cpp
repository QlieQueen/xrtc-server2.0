#include "modules/rtp_rtcp/receive_stat.h"

#include <rtc_base/logging.h>

namespace xrtc {

namespace {

const int kMaxReorderingThreshold = 450;

} // namespace

StreamStat::StreamStat(uint32_t ssrc, webrtc::Clock* clock) :
    ssrc_(ssrc),
    clock_(clock),
    max_reordering_threshold_(kMaxReorderingThreshold)
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

    // jitter 统计: 同一帧的多个 RTP 切片共享同一个时间戳(Timestamp),
    // 它们之间的到达间隔是切片/突发传输的节奏, 不是网络抖动, 必须跳过;
    // 首包没有上一个包可比较, 用 packets>1 兜底避免假巨抖动
    if (packet.Timestamp() != last_received_timestamp_ && (
                receive_counters_.transmitted.packets -
                receive_counters_.retransmitted.packets) > 1)
    {
        UpdateJitter(packet, now_ms);
    }

    // 无条件更新上一包状态: 同帧后续片也刷新到达时间,
    // 使 jitter 采样点是"上帧末片 -> 新帧首片"的帧间节奏
    last_received_timestamp_ = packet.Timestamp();
    last_received_time_ms_ = now_ms;
}

void StreamStat::UpdateJitter(const webrtc::RtpPacketReceived& packet,
        int64_t receive_time)
{
    // R 侧: 到达时间差 Rj - Ri, 毫秒换算成 RTP tick (与时间戳同单位才能相减)
    int64_t receive_time_diff = receive_time - last_received_time_ms_; // Rj - Ri(毫秒)
    uint32_t receive_diff_rtp = static_cast<uint32_t>(
        receive_time_diff * packet.payload_type_frequency() / 1000);   // -> tick(视频 90k)

    // S 侧: 发送时间戳差 Sj - Si, 本就是 RTP tick
    // D(i,j) = (Rj - Ri) - (Sj - Si) = 包 j 端到端延迟 - 包 i 端到端延迟
    // (差值形式自动抵消两端时钟不同步等固定偏移, 只留变化的部分)
    int32_t time_diff_samples = receive_diff_rtp -
        (packet.Timestamp() - last_received_timestamp_);

    // 取绝对值: 抖动只关心幅度, 不关心是提前还是滞后
    time_diff_samples = std::abs(time_diff_samples);
    // 450000 = 5s x 90kHz: 网络停顿超 5 秒是异常事件, 假巨抖动会污染滤波, 丢弃
    if (time_diff_samples < 450000) {
        // Q4 定点: jitter_q4_ 存 Jx16, |D| 也放大 16 倍(<<4)对齐才能相减
        int32_t jitter_q4_diff = (time_diff_samples << 4) - jitter_q4_; // 16x(|D| - J)
        // J(i) = J(i-1) + (|D| - J(i-1))/16, 两边x16 后 /16 约掉, 只剩原量级差值;
        // (x + 8) >> 4 = round(x/16): +8 是舍入偏置, >>4 还原量级
        jitter_q4_ += ((jitter_q4_diff + 8) >> 4);
    }
}

// 乱序/序列突变判定: 4.6 骨架恒 true (所有非首包走乱序分支, 统计值不完整),
// 4.7 按阈值 450 实现: 比前沿旧 → 乱序/重传; 比前沿大太多 → 序列突变
bool StreamStat::UpdateOutOfOrder(const webrtc::RtpPacketReceived& packet,
    int64_t sequence_number,
    int64_t /*now_ms*/)
{
    // 检车是否发生突变
    if (received_seq_out_of_order_) {
        uint16_t expected_seq_num = *received_seq_out_of_order_ + 1;
        received_seq_out_of_order_ = absl::nullopt;
        if (packet.SequenceNumber() == expected_seq_num) { // 认定发生序列号突变，重置计数状态
            received_seq_max_ = sequence_number - 2;
            return false;
        }
    }

    // 有可能是流序列号发生突变了
    if (abs(sequence_number - received_seq_max_) > max_reordering_threshold_) {
        received_seq_out_of_order_ = packet.SequenceNumber();
        ++cumulative_loss_;
        return true;
    }

    if (sequence_number > received_seq_max_) { // 丢包
        return false;
    }
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
