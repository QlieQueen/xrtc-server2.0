#ifndef XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_
#define XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_

#include <map>
#include <set>
#include <vector>

#include <system_wrappers/include/clock.h>
#include <rtc_base/numerics/sequence_number_util.h>
#include <rtc_base/third_party/sigslot/sigslot.h>

#include "base/event_loop.h"
#include "modules/video_coding/histogram.h"

namespace xrtc {

class NackRequester {
public:
    NackRequester(webrtc::Clock* clock, EventLoop* el);
    ~NackRequester();

    // 定时器入口: 周期触发 kTimeOnly 重传(RTT 退避点名)
    void ProcessNacks();
    // 外部喂入实测 RTT(重传间隔依据; 当前无人调用, 用默认 100ms)
    void UpdateRtt(int64_t rtt_ms) { rtt_ms_ = rtt_ms; }

    // 返回某序号包的重传次数(times_nacked, 流入 Packet::times_nacked)
    // 旧包分支返回: 重传补到 → 该包被重传过几次; 纯乱序 → 0
    // is_keyframe: 本包是否关键帧首包(其 seq 记入 keyframe_list_ 锚点)
    int OnReceivedPacket(uint16_t seq_num, bool is_keyframe, bool is_retransmitted);

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
        NackInfo() : seq_num(0), send_at_seq_num(0),
            created_time(-1), send_at_time(-1),
            retries(0) {}
        NackInfo(uint16_t seq_num, uint16_t send_at_seq_num, int64_t created_time) :
            seq_num(seq_num),
            send_at_seq_num(send_at_seq_num),
            created_time(created_time),
            send_at_time(-1), retries(0) {}
        uint16_t seq_num;        // 缺失包的序号
        uint16_t send_at_seq_num; // 序号等待线: 前沿推进到此处仍缺才判定真丢(seq + N, N 来自乱序直方图)
        int64_t created_time;    // 记入缺失的时间
        int64_t send_at_time;    // 上次对该包发 NACK 的时间(-1 = 还没发过, 6.1.3 用)
        int retries;             // 已重传次数(重传超限放弃的判断依据)
    };

    void AddPacketsToNack(uint16_t seq_start, uint16_t seq_num_end);
    std::vector<uint16_t> GetNackBatch(NackFilterOptions option);
    // nack_list_ 超限时清掉关键帧之前的缺失包(参考链已断, 重传无用), 腾空间
    bool RemovePacketsUntilKeyFrame();
    void UpdateReorderingStat(uint16_t seq_num);
    size_t WaitNumberOfPackets(float probability);

private:
    webrtc::Clock* clock_;
    EventLoop* el_;
    TimerWatcher* nack_timer_ = nullptr;   // 20ms 周期定时器 → ProcessNacks
    bool initialized_ = false;   // 是否收到过第一个包
    // 接收"前沿": 已收到包中发送端最晚发出的序号
    // 注意"新旧" = 发送端发出的先后, 与到达顺序无关(回绕后数值大小会反转, 见 v2_6.1.2 笔记)
    uint16_t newest_seq_num_ = 0;
    std::map<uint16_t, NackInfo, webrtc::DescendingSeqNumComp<uint16_t>> nack_list_;
    // 关键帧首包 seq 锚点表: 超限清理时批量放弃"关键帧之前的缺失包"
    std::set<uint16_t, webrtc::DescendingSeqNumComp<uint16_t>> keyframe_list_;
    // 重传间隔基准(两次重传之间至少等这么久); 默认 100ms, 可 UpdateRtt 喂实测值
    int64_t rtt_ms_;
    // 乱序等待窗口(ms): 延迟首次点名给乱序包到达时间; 0 = 不等待(预留, 无人设置)
    int64_t send_nack_delay_ms_ = 0;
    Histogram reordering_histogram_; // 乱序距离直方图: WaitNumberOfPackets 查等待线偏移 N
};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_