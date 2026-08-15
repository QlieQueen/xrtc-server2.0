#ifndef XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_
#define XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_

#include <map>
#include <vector>

#include <system_wrappers/include/clock.h>
#include <rtc_base/numerics/sequence_number_util.h>
#include <rtc_base/third_party/sigslot/sigslot.h>

namespace xrtc {

class NackRequester {
public:
    NackRequester(webrtc::Clock* clock);
    ~NackRequester();

    // 返回某序号包的重传次数(times_nacked, 流入 Packet::times_nacked)
    // 旧包分支返回: 重传补到 → 该包被重传过几次; 纯乱序 → 0
    int OnReceivedPacket(uint16_t seq_num);

public:
    sigslot::signal1<const std::vector<uint16_t>> SignalNackSend;

private:
    enum NackFilterOptions {
        kSeqNumOnly, // 基于丢包时触发
        kTimeOnly,   // 定时触发
        kSeqNumAndTime, // 同时触发
    };

    // 缺失包档案: "曾经判定为丢失"的包 + 重传控制信息
    // DescendingSeqNumComp = 回绕感知降序比较(不用 std::less:
    // 16位 seq 回绕后数值大小关系反转, 排序必须回绕安全)
    struct NackInfo {
        NackInfo() : seq_num(0), created_time(-1), send_at_time(-1),
            retries(0) {}
        NackInfo(uint16_t seq_num, int64_t created_time) :
            seq_num(seq_num), created_time(created_time),
            send_at_time(-1), retries(0) {}
        uint16_t seq_num;        // 缺失包的序号
        int64_t created_time;    // 记入缺失的时间
        int64_t send_at_time;    // 上次对该包发 NACK 的时间(-1 = 还没发过, 6.1.3 用)
        int retries;             // 已重传次数(重传超限放弃的判断依据)
    };

    void AddPacketsToNack(uint16_t seq_start, uint16_t seq_num_end);
    std::vector<uint16_t> GetNackBatch(NackFilterOptions option);

private:
    webrtc::Clock* clock_;
    bool initialized_ = false;   // 是否收到过第一个包
    // 接收"前沿": 已收到包中发送端最晚发出的序号
    // 注意"新旧" = 发送端发出的先后, 与到达顺序无关(回绕后数值大小会反转, 见 v2_6.1.2 笔记)
    uint16_t newest_seq_num_ = 0;
    std::map<uint16_t, NackInfo, webrtc::DescendingSeqNumComp<uint16_t>> nack_list_;
};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_