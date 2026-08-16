#include "modules/video_coding/histogram.h"

namespace xrtc {

Histogram::Histogram(size_t num_buckets, size_t max_num_packets)
{
    buckets_.resize(num_buckets);
    values_.reserve(max_num_packets);
}

Histogram::~Histogram() {}

void Histogram::Add(size_t value) {
    // 超过桶数的距离 clamp 进最后一桶
    value = std::min<size_t>(value, buckets_.size() - 1);
    if (index_ < values_.size()) { // 窗口已满: 覆盖最老样本, 先把它从桶计数剔除
        --buckets_[values_[index_]];
        values_[index_] = value;
    } else {
        values_.emplace_back(value); // 窗口未满: 追加
    }

    ++buckets_[value];
    index_ = (index_ + 1) % values_.capacity();
}

size_t Histogram::InverseCdf(float probability) const {
    size_t bucket = 0;
    float accumulated_probability = 0;
    // 从桶 0 往右累加占比, 累计达到 probability 时的桶下标 = 分位数 N
    while (accumulated_probability < probability && bucket < buckets_.size()) {
        accumulated_probability += (float)(buckets_[bucket]) / values_.size();
        ++bucket;
    }

    return bucket;
}

size_t Histogram::NumValues() const {
    return values_.size();
}

} // namespace xrtc
