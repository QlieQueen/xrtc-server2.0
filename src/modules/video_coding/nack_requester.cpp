#include "modules/video_coding/nack_requester.h"

#include <rtc_base/logging.h>

namespace xrtc {

NackRequester::NackRequester(webrtc::Clock* clock) :
    clock_(clock)
{

}

NackRequester::~NackRequester() {

}

int NackRequester::OnReceivedPacket(uint16_t seq_num) {
    //RTC_LOG(LS_WARNING) << "=============seq_num: " << seq_num;
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

    // ④ 新包分支: seq_num 比前沿更新(发送端更晚发出)
    //    (6.1.3 补全: 更新前沿 + 检查"前沿到 seq_num 之间的缺口" → 缺口即缺失包, 记入 nack_list_)


    return 0;
}

} // namespace xrtc
