#include "modules/video_coding/nack_requester.h"

#include <rtc_base/logging.h>

namespace xrtc {

namespace {

const int kMaxNackRetries = 10;    // 单个包最大重传次数, 超限放弃(等关键帧兜底)
const int kUpdateIntervalMs = 20;  // NACK 检查节拍: 每 20ms 定时触发一次重传判定
const int64_t kDefaultRttMs = 100; // 重传间隔默认值(ms): 无实测 RTT 时两次重传至少等 100ms

void nack_timer_cb(EventLoop*, TimerWatcher*, void* data) {
    NackRequester* requester = (NackRequester*)data;
    requester->ProcessNacks();
}

} // namespace

NackRequester::NackRequester(webrtc::Clock* clock, EventLoop* el) :
    clock_(clock),
    el_(el),
    rtt_ms_(kDefaultRttMs)
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

int NackRequester::OnReceivedPacket(uint16_t seq_num) {
    // ① 首包: 初始化接收前沿(newest_seq_num_), 还没有历史可比较
    if (!initialized_) {
        newest_seq_num_ = seq_num;
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
        return nacks_send_for_packet;
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

// 把 (start, end) 区间内所有序号记入缺失档案
void NackRequester::AddPacketsToNack(uint16_t seq_num_start, uint16_t seq_num_end) {
    for (uint16_t  seq_num = seq_num_start; seq_num != seq_num_end; ++seq_num) {
        NackInfo nack_info(seq_num, clock_->TimeInMilliseconds());
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
        // 丢包触发: 没发过 NACK 的包(send_at_time == -1)首次点名
        bool can_nack_seq_num_passed = (it->second.send_at_time == -1);
        // 定时触发: 距上次重传已超过 RTT, 再点一次名(防重传包也丢了, 两次间隔≥RTT)
        bool can_nack_timestamp_passed = (now - it->second.send_at_time) > rtt_ms_;

        if ((consider_seq_num && can_nack_seq_num_passed) ||
            (consider_timestamp && can_nack_timestamp_passed))
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
