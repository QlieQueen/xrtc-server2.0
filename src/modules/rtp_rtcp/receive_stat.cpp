#include "modules/rtp_rtcp/receive_stat.h"

#include <rtc_base/logging.h>

namespace xrtc {

namespace {

const int kMaxReorderingThreshold = 450;
const int kStreamStatTimeoutMs = 8000;

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

// 报告块填充 + 周期基线更新(AndReset):
// 把 4.6~4.8 攒的状态换算成 report block 四个字段:
// fraction lost(本周期差值) / cumulative lost(全程累计+负值偏移+封顶) /
// ext highest seq / jitter (见 note/v2_4.10-RR包报告块深度拆解)
void StreamStat::MaybeAppendReportBlockAndReset(
        std::vector<webrtc::rtcp::ReportBlock>& result) {
    int64_t now_ms = clock_->TimeInMilliseconds();
    // 超时保护: 8s 没收到包说明流已停, 不发陈旧统计(最后包到达后不再上报)
    if (now_ms - last_received_time_ms_ > kStreamStatTimeoutMs) {
        return;
    }

    // 从来没收到过包, 没有可报的统计
    if (!ReceivedRtpPacket()) {
        return;
    }

    // 生成report block, 先写"被报告的发送端媒体 ssrc"(本桶是谁就报谁)
    result.emplace_back();
    webrtc::rtcp::ReportBlock& stats = result.back(); // 引用尾部元素
    stats.SetMediaSsrc(ssrc_);

    // fraction lost(8bit 定点小数, 0~255 映射 0~100%): 差值法取"本报告周期"的丢包比例
    int64_t exp_since_last = received_seq_max_ - last_report_seq_max_;      // 本周期期望收(相对上次报告基线)
    int32_t loss_since_last = cumulative_loss_ - last_report_cumulative_loss_; // 本周期丢包增量
    // 255 而非 256: 字段 8bit 无符号, 100% 丢时 256 会溢出成 0; 255*比例永不溢出
    // 两值都 >0 才填: loss<=0 表示本周期没丢(字段默认 0 即可), exp<=0 会除零
    if (exp_since_last > 0 && loss_since_last > 0) {
        stats.SetFractionLost(255 * loss_since_last / exp_since_last);
    }

    // cumulative lost(24bit 有符号, 全程累计): 内部账本 cumulative_loss_ 可为负
    // (重传/乱序时实收>期望, 见 4.7 账本模型), 但字段必须非负;
    // 负值时用 offset 线性平移而不是 clamp(非线性):
    // clamp 把负区间信息抹掉, 破坏接收端"相邻 RR 差分"的准确性;
    // offset 保持差分不变, 报出值恒非负且单调
    int32_t packets_lost = cumulative_loss_ + cumulative_loss_rtcp_offset_;
    if (packets_lost < 0) {
        packets_lost = 0;
        // 偏移定格"历史最深负值": 之后报出 = 内部 + 偏移 恒非负;
        // 仅当内部跌破新纪录时才再次触发刷新
        cumulative_loss_rtcp_offset_ = -cumulative_loss_;
    }

    // 24bit 有符号上限 2^23-1, 超过会溢出成负数(写 3 字节符号位翻转);
    // 封顶到最大值, 且只告警一次(避免每周期刷屏)
    if (packets_lost > 0x7FFFFF) {
        if (!cumulative_loss_is_capped_) {
            cumulative_loss_is_capped_ = true;
            RTC_LOG(LS_WARNING) << "cumulative packet loss reached max value for ssrc: "
                << ssrc_;
        }
        packets_lost = 0x7FFFFF;
    }

    stats.SetCumulativeLost(packets_lost);
    stats.SetExtHighestSeqNum(received_seq_max_);
    // jitter 是 EWMA 状态, 天然是当前值, 直接 >>4 还原上报(4.8 的 Q4 定点)
    stats.SetJitter(jitter_q4_ >> 4);

    // AndReset 重置的是"上次报告基线"(两个快照), 不是累计账本:
    // 基线供下周期 fraction lost 差值对比; cumulative_loss_ 原样保留
    last_report_seq_max_ = received_seq_max_;
    last_report_cumulative_loss_ = cumulative_loss_;
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
        // 首次建桶登记 ssrc, 供 RtcpReportBlocks 轮询遍历
        all_ssrcs_.push_back(ssrc);
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

// 多 SSRC 公平轮询: 一个 RR 包的 Report Block Count 只有 5bit(最多 31 块),
// ssrc 超过上限时一次装不下, 从上次停下的位置(last_returned_ssrc_idx_ + 1)环形继续,
// 让每个 ssrc 在各周期间均匀地被上报, 不会饿死排在后面的 ssrc
std::vector<webrtc::rtcp::ReportBlock> ReceiveStat::RtcpReportBlocks(size_t max_blocks) {
    std::vector<webrtc::rtcp::ReportBlock> result;
    result.reserve(std::min(max_blocks, all_ssrcs_.size()));

    size_t ssrc_idx = 0;
    for (size_t i = 0; i < all_ssrcs_.size() && result.size() < max_blocks; ++i) {
        ssrc_idx = (last_returned_ssrc_idx_ + 1 + i) % all_ssrcs_.size();
        uint32_t media_ssrc = all_ssrcs_[ssrc_idx];
        auto stat_iter = stats_.find(media_ssrc);
        stat_iter->second->MaybeAppendReportBlockAndReset(result);
    }

    // 循环溢出后 ssrc_idx 停在最后处理的位置, 作为下周期的起点
    last_returned_ssrc_idx_ = ssrc_idx;

    return result;
}

} // namespace xrtc
