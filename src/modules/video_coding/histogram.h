#ifndef XRTCSERVER_MODULES_VIDEO_CODING_HISTOGRAM_H_
#define XRTCSERVER_MODULES_VIDEO_CODING_HISTOGRAM_H_

#include <vector>
#include <cstddef>

namespace xrtc {

// 乱序距离直方图: 维护最近 max_num_packets 个样本的分布, 供 InverseCdf 查分位数
class Histogram {
public:
    // num_buckets: 桶数(乱序距离上限, 超出的 clamp 进最后一桶)
    // max_num_packets: 样本窗口大小(环形缓冲, 满了覆盖最老的)
    Histogram(size_t num_buckets, size_t max_num_packets);
    ~Histogram();

    // 喂入一个样本(乱序距离), 窗口满了覆盖最老样本
    void Add(size_t value);
    // 返回 N: 至少 probability 比例的样本(乱序距离) <= N
    size_t InverseCdf(float probability) const;
    size_t NumValues() const;

private:
    std::vector<size_t> values_;  // 样本环形缓冲: 存最近 max_num_packets 个乱序距离
    std::vector<size_t> buckets_; // 计数桶: buckets_[i] = 乱序距离 i 的样本个数
    size_t index_ = 0;            // 环形缓冲写入游标(下一个样本写入的位置)
};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_VIDEO_CODING_HISTOGRAM_H_
