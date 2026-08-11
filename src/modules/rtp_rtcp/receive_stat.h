#ifndef XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_
#define XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_

#include <memory>

#include <rtc_base/containers/flat_map.h>
#include <system_wrappers/include/clock.h>
#include <modules/rtp_rtcp/source/rtp_packet_received.h>
#include <modules/rtp_rtcp/source/rtcp_packet/report_block.h>
#include <modules/rtp_rtcp/include/rtp_rtcp_defines.h>
#include <modules/include/module_common_types_public.h>

namespace xrtc {

// per-SSRC 统计桶: 每个 SSRC 一个独立实例, 各自累计自己的
// 序列号/到达时间. 主媒体流与 RTX 流的 seq 独立递增,
// 必须分桶统计, 否则后续丢包/jitter 计算会串流
class StreamStat {
public:
    StreamStat(uint32_t ssrc, webrtc::Clock* clock);
    ~StreamStat();

    // 每包累计计数, 当前空壳, 4.6 填充 seq 解绕/丢包统计
    void UpdateCounters(const webrtc::RtpPacketReceived& packet);
    // 把本桶一个周期的统计填进 report block (fraction lost/累计丢包/ext seq/jitter),
    // 并重置周期账本供下一周期用; 4.10 空壳, 4.11 填实
    void MaybeAppendReportBlockAndReset(std::vector<webrtc::rtcp::ReportBlock>& result);

private:
    bool ReceivedRtpPacket() const { return received_seq_first_ >= 0; }
    bool UpdateOutOfOrder(const webrtc::RtpPacketReceived& packet,
        int64_t sequence_number,
        int64_t now_ms);
    void UpdateJitter(const webrtc::RtpPacketReceived& packet,
            int64_t now_ms);

private:
    uint32_t ssrc_;
    webrtc::Clock* clock_;
    webrtc::StreamDataCounters receive_counters_;
    int max_reordering_threshold_;
    absl::optional<uint16_t> received_seq_out_of_order_;

    webrtc::Unwrapper<uint16_t> seq_unwrapper_;

    // 累计丢包数(账本): 每包到货 -1, 顺序新包补差 +(seq - max),
    // 全部到齐账必平; 存在非rtx的重传包时可能为负值
    int32_t cumulative_loss_ = 0;
    int64_t received_seq_first_ = -1;  // 首个包的扩展seq, -1 表示未收到
    int64_t received_seq_max_ = -1;    // 顺序前沿: 已收到包的最大扩展seq, 乱序包不更新

    uint32_t last_received_timestamp_ = 0;
    int64_t last_received_time_ms_ = 0;
    uint32_t jitter_q4_ = 0;
};

// 接收统计模块: 为 RTCP RR (Receiver Report) 提供数据源.
// 每个到达的 RTP 包调一次 OnRtpPacket, 累计序列号/到达时间等原始数据,
// 后续课程在此基础上统计丢包/乱序/jitter, 并生成 RR 的 report blocks (4.10)
class ReceiveStat {
public:
    ReceiveStat(webrtc::Clock* clock);
    ~ReceiveStat();

    // 工厂方法: 统一入口创建统计模块
    static std::unique_ptr<ReceiveStat> Create(webrtc::Clock* clock);
    void OnRtpPacket(const webrtc::RtpPacketReceived& packet);
    // 产出最多 max_blocks 个报告块: 多 SSRC 时环形轮询, 每周期从上次停下的位置继续,
    // 保证一个 RR 包(最多 31 块)装不下时各 ssrc 公平上报
    std::vector<webrtc::rtcp::ReportBlock> RtcpReportBlocks(size_t max_blocks);

    // 按 ssrc 取统计桶, 不存在则惰性创建 (SSRC 是 answer 协商后才知道的,
    // 主媒体与 rtx 两个 ssrc 知晓时机不同, 按需创建)
    StreamStat* GetOrCreateStat(uint32_t ssrc);

private:
    webrtc::Clock* clock_;   // 时钟: 记录包到达时间, jitter 计算的时间基准
    // ssrc → 统计桶. flat_map: 小容器连续内存 + 二分查找,
    // SSRC 数量极小(1~2个), 比 unordered_map 缓存友好
    webrtc::flat_map<uint32_t, std::unique_ptr<StreamStat>> stats_;
    // 建过桶的 ssrc 登记表: 轮询要"上次位置"这个游标跨周期存活,
    // 而 flat_map 是连续内存, 插入会搬移元素使索引/迭代器失效, 故另存稳定索引
    std::vector<uint32_t> all_ssrcs_;
    size_t last_returned_ssrc_idx_ = 0; // 轮询游标: 上次上报到的位置, 下次从其后继续
};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_RTP_RTCP_RECEIVE_STAT_H_
