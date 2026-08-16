#include "modules/video_coding/nack_requester.h"

#include <rtc_base/logging.h>

namespace xrtc {

namespace {

const int kMaxNackRetries = 10;    // 单个包最大重传次数, 超限放弃(等关键帧兜底)
const int kUpdateIntervalMs = 20;  // NACK 检查节拍: 每 20ms 定时触发一次重传判定
const int64_t kDefaultRttMs = 100; // 重传间隔默认值(ms): 无实测 RTT 时两次重传至少等 100ms
const uint16_t kMaxPacketAge = 10000; // 缺失包过期线: 距最新序号超过 10000 不再追
const int kMaxNackPackets = 1000; // nack_list_ 容量上限: 超限清关键帧前的缺失包, 仍超限则全清+请求关键帧
const int kMaxReorderingPackets = 128;
const int kNumReoderingBuckets = 10;

void nack_timer_cb(EventLoop*, TimerWatcher*, void* data) {
    NackRequester* requester = (NackRequester*)data;
    requester->ProcessNacks();
}

} // namespace

NackRequester::NackRequester(webrtc::Clock* clock, EventLoop* el) :
    clock_(clock),
    el_(el),
    rtt_ms_(kDefaultRttMs),
    reordering_histogram_(kNumReoderingBuckets, kMaxReorderingPackets)
{
    // 注册 20ms 周期定时器: 驱动 kTimeOnly 重传(RTT 退避再次点名)
    nack_timer_ = el_->CreateTimer(nack_timer_cb, this, true);
    el_->StartTimer(nack_timer_, kUpdateIntervalMs * 1000);
}

NackRequester::~NackRequester() {
}

void NackRequester::ProcessNacks() {
    // 定时触发: 重传过的包按 RTT 退避再次点名(send_at_time 已置位的包)
    auto nack_batch = GetNackBatch(kTimeOnly);
    if (!nack_batch.empty()) {
        SignalNackSend(nack_batch);
    }
}

int NackRequester::OnReceivedPacket(uint16_t seq_num, bool is_keyframe,
        bool is_retransmitted)
{
    // ① 首包: 初始化接收前沿(newest_seq_num_), 还没有历史可比较
    if (!initialized_) {
        newest_seq_num_ = seq_num;
        if (is_keyframe) {
            keyframe_list_.insert(seq_num);
        }
        initialized_ = true;
        return 0;
    }

    // ② 重复包: 与前沿同号, 直接忽略
    if (seq_num == newest_seq_num_) {
        return 0;
    }

    // ③ 旧包分支: seq_num 比前沿"旧"(发送端更早发出, 见 v2_6.1.2 笔记新旧语义)
    // 注意: 必须用 AheadOf 回绕感知比较, 不能写成 seq_num < newest_seq_num_
    //     (16位 seq 回绕后数值大小反转, 如 AheadOf(65510, 2) == 0, 2 反而更新)
    // 旧包只有两种身份:
    //     - 乱序包: 绕路晚到, 从没判定过丢失 → nack_list_ 里查不到
    //     - 重传包: 之前判定丢失记过档 → nack_list_ 里查得到 → 补齐了, 删档
    /*
    uint16_t a = 8;
    uint16_t b = 2;
    RTC_LOG(LS_WARNING) << "==========webrtc::AheadOf(8, 2) = " << webrtc::AheadOf(a, b);
    a = 65510;
    RTC_LOG(LS_WARNING) << "==========webrtc::AheadOf(65510, 2) = " << webrtc::AheadOf(a, b);
    */
    if (webrtc::AheadOf(newest_seq_num_, seq_num)) {
        // 查缺失档案: 在档 = 重传补到, 返回它被重传过几次(times_nacked)并删档
        // 不在档 = 纯乱序, 返回 0(不用 NACK, 乱序的包自己会到)
        auto nack_list_it = nack_list_.find(seq_num);
        int nacks_send_for_packet = 0;
        if (nack_list_it != nack_list_.end()) {
            nacks_send_for_packet = nack_list_it->second.retries;
            nack_list_.erase(nack_list_it);
        }

        if (!is_retransmitted) {
            // 乱序包
            UpdateReorderingStat(seq_num);
        }

        return nacks_send_for_packet;
    }

    if (is_keyframe) {
        keyframe_list_.insert(seq_num);  // 记关键帧首包锚点(超限清理时定位"还能救的包")
    }

    // 锚点也按 kMaxPacketAge 清过期, 防 keyframe_list_ 无限膨胀
    auto it = keyframe_list_.lower_bound(seq_num - kMaxPacketAge);
    if (it != keyframe_list_.begin()) {
        keyframe_list_.erase(keyframe_list_.begin(), it);
    }

    // ④ 新包分支: 前沿推进, 沿途缺口记入缺失档案, 触发一次 NACK 点名
    AddPacketsToNack(newest_seq_num_ + 1, seq_num);
    newest_seq_num_ = seq_num;

    std::vector<uint16_t> nack_batch = GetNackBatch(kSeqNumOnly);
    if (!nack_batch.empty()) {
        SignalNackSend(nack_batch);
    }

    return 0;
}

// 乱序统计: 旧包(非重传)到达时, 记它与前沿的距离入直方图
void NackRequester::UpdateReorderingStat(uint16_t seq_num) {
    size_t diff = webrtc::ReverseDiff(newest_seq_num_, seq_num);
    reordering_histogram_.Add(diff);
}

// 查乱序直方图分位数: 返回 N, 至少 probability 比例的乱序包在 N 个序号内到达
// 直方图无样本时返回 0(不等待, 直方图接入前退化为原行为)
size_t NackRequester::WaitNumberOfPackets(float probability) {
    if (reordering_histogram_.NumValues() == 0) {
        return 0;
    }

    return reordering_histogram_.InverseCdf(probability);
}

// 超限清理: 删掉关键帧之前的缺失包(参考链已断, 重传也白费), 腾空间记新缺口
bool NackRequester::RemovePacketsUntilKeyFrame() {
    while (!keyframe_list_.empty()) {
        auto it = nack_list_.lower_bound(*keyframe_list_.begin());
        if (it != nack_list_.begin()) {
            nack_list_.erase(nack_list_.begin(), it);
            return true;
        }

        // 该锚点前已无缺失包可清, 弃用换下一个锚点
        keyframe_list_.erase(keyframe_list_.begin());
    }

    return false;
}

// 把 (start, end) 区间内所有序号记入缺失档案
void NackRequester::AddPacketsToNack(uint16_t seq_num_start, uint16_t seq_num_end) {
    // 清掉距最新序号(seq_num_end)超过 kMaxPacketAge 的过期缺失包
    // lower_bound 返回过期线边界迭代器, 其前的包都太旧, 不再追(等关键帧兜底)
    auto it = nack_list_.lower_bound(seq_num_end - kMaxPacketAge);
    nack_list_.erase(nack_list_.begin(), it);

    // 判断添加完新的seq_num之后，nack_list_的长度是否超过限制
    // 1.计算当前新添加seq_num的个数
    int new_nack_num = webrtc::ForwardDiff(seq_num_start, seq_num_end);
    // 2.判断是否超过限制
    if (nack_list_.size() + new_nack_num > kMaxNackPackets) {
        // 超过了限制，需要清理
        while (RemovePacketsUntilKeyFrame() &&
                nack_list_.size() + new_nack_num > kMaxNackPackets) { }
    
        // 尽最大努力清理，但是仍然无法满足要求，放弃重传，直接请求关键帧
        if (nack_list_.size() + new_nack_num > kMaxNackPackets) {
            nack_list_.clear();
            // TODO: 请求关键帧
            RTC_LOG(LS_WARNING) << "nack_list full, clear nack_list and request keyframe";
            return;
        }
    }

    for (uint16_t  seq_num = seq_num_start; seq_num != seq_num_end; ++seq_num) {
        // 等待线 = 缺口序号 + N: 等前沿推进到等待线仍缺, 才判定真丢点名(乱序包等期内自己到)
        NackInfo nack_info(seq_num, seq_num + WaitNumberOfPackets(0.5),
                clock_->TimeInMilliseconds());
        nack_list_[seq_num] = nack_info;
    }
}

// 首次点名: 只挑没发过 NACK 的包(send_at_time == -1), 置重传次数/时间, 超 10 次放弃
std::vector<uint16_t> NackRequester::GetNackBatch(NackFilterOptions option) {
    bool consider_seq_num = (option != kTimeOnly);
    bool consider_timestamp = (option != kSeqNumOnly); // 定时触发分支
    int64_t now = clock_->TimeInMilliseconds();
    std::vector<uint16_t> nack_batch;
    auto it = nack_list_.begin();
    while (it != nack_list_.end()) {
        // 乱序等待闸门: 缺口创建后至少等 send_nack_delay_ms_ 才允许首次点名(给乱序包到达时间)
        bool delay_timeout = (now - it->second.created_time) >= send_nack_delay_ms_;
        // 丢包触发: 没发过 NACK 且前沿已推进到等待线(send_at_seq_num = seq + N, 等够 N 个位置)才首次点名
        bool can_nack_seq_num_passed = (it->second.send_at_time == -1) &&
            webrtc::AheadOrAt(newest_seq_num_, it->second.send_at_seq_num);
        // 定时触发: 距上次重传已超过 RTT, 再点一次名(防重传包也丢了, 两次间隔≥RTT)
        bool can_nack_timestamp_passed = (now - it->second.send_at_time) > rtt_ms_;

        if (delay_timeout && ((consider_seq_num && can_nack_seq_num_passed) ||
            (consider_timestamp && can_nack_timestamp_passed)))
        {
            // 触发nack的发送
            nack_batch.emplace_back(it->second.seq_num);
            ++it->second.retries;
            it->second.send_at_time = now;
            // 当该包重传的次数已经达到10次，不要再重传了
            if (it->second.retries >= kMaxNackRetries) {
                RTC_LOG(LS_WARNING) << "sequence number: " << it->second.seq_num
                    << " removed from nack list due to max retries";
                nack_list_.erase(it++);
            } else {
                ++it;
            }

            continue;
        }
        ++it;
    }

    return nack_batch;
}


} // namespace xrtc
