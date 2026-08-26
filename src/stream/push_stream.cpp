/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file push_stream.cpp
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/

#include "stream/push_stream.h"

#include <rtc_base/logging.h>

namespace xrtc {

PushStream::PushStream(EventLoop* el, PortAllocator* allocator, 
        uint64_t uid, const std::string& stream_name,
        bool audio, bool video, uint32_t log_id) :
    RtcStream(el, allocator, uid, stream_name, audio, video, log_id),
    video_data_buffer_(kVideoPacketCacheSize)
{
    pc->SignalRtpPacket.connect(this, &PushStream::OnRtpPacket);
    pc->SignalFrame.connect(this, &PushStream::OnFrame);
}

PushStream::~PushStream() {
    RTC_LOG(LS_INFO) << ToString() << ": Push stream destroy.";
}

std::string PushStream::CreateOffer() {
    RTCOfferAnswerOptions options;
    options.send_audio = false;
    options.send_video = false;
    options.recv_audio = audio;
    options.recv_video = video;

    return pc->CreateOffer(options);
}

bool PushStream::GetAudioSource(std::vector<StreamParams>& source) {
    return GetSource("audio", source);
}

bool PushStream::GetVideoSource(std::vector<StreamParams>& source) {
    return GetSource("video", source);
}

bool PushStream::GetSource(const std::string& mid, std::vector<StreamParams>& source) {
    if (!pc) {
        return false;
    }

    auto remote_desc = pc->remote_desc();
    if (!remote_desc) {
        return false;
    }

    auto content = remote_desc->GetContent(mid);
    if (!content) {
        return false;
    }

    source = content->streams();
    return true;
}

std::shared_ptr<RtcPacket> PushStream::FindVideoPacket(uint16_t seq_num) {
    size_t index = seq_num % kVideoPacketCacheSize;
    if (video_data_buffer_[index] && video_data_buffer_[index]->seq_num == seq_num) {
        return video_data_buffer_[index];
    }

    return nullptr;
}

void PushStream::CacheVideoPacket(std::shared_ptr<RtcPacket> packet) {
    uint16_t seq_num = packet->seq_num;
    size_t index = seq_num % kVideoPacketCacheSize;

    // 过滤重复的数据
    if (video_data_buffer_[index] && video_data_buffer_[index]->seq_num == seq_num) {
        return;
    }

    video_data_buffer_[index] = packet;
}

void PushStream::OnRtpPacket(PeerConnection*, webrtc::MediaType media_type,
        const webrtc::RtpPacketReceived& packet)
{
    // 缓存rtp packet，用于重传
    std::shared_ptr<RtcPacket> new_packet = std::make_shared<RtcPacket>(
            packet.SequenceNumber(), media_type, false,
            packet.arrival_time().ms(),
            packet.data(), packet.size());

    if (webrtc::MediaType::VIDEO == media_type) {
        ProcessVideoPacket(new_packet);
    } else {
        if (listener_) {
            listener_->OnRtpPacket(this, webrtc::MediaType::AUDIO, new_packet);
        }
    }
}

void PushStream::ProcessVideoPacket(std::shared_ptr<RtcPacket> packet) {
    // 1. 缓存rtp packet，用于重传
    CacheVideoPacket(packet);

    // 2. rtp包的连续性控制，如果当前的rtp包不连续，暂时不转发
    uint16_t seq_num = packet->seq_num;
    if (-1 == first_seq_time_) {  // 收到第一个包
        video_seq_ = packet->seq_num;
        first_seq_time_ = packet->ts;
        if (listener_) {
            listener_->OnRtpPacket(this, webrtc::MediaType::VIDEO, packet);
        }
        return;
    }

    // 期待的下一个包的序列号
    uint16_t expected_seq_num = video_seq_ + 1;

    if (webrtc::AheadOrAt(video_seq_, seq_num)) { // 重复的包
        return;
    } else if (seq_num != expected_seq_num) { // 发生了丢包或者乱序
        return;
    } else { // 正常有序的包
        int test_size = 0;

        do {
            ++test_size;
            auto video_packet = FindVideoPacket(seq_num);
            if (!video_packet) {
                break;
            }

            if (listener_) {
                listener_->OnRtpPacket(this, webrtc::MediaType::VIDEO, video_packet);
            }
            seq_num++;

        } while (test_size <= kVideoPacketCacheSize);

        video_seq_ = seq_num - 1;
    }
}

void PushStream::OnFrame(PeerConnection*, RtpFrameObject* frame) {
    if (!frame || frame->frame_type() != webrtc::VideoFrameType::kVideoFrameKey) {
        return;
    }

    // 此时表明收到了完整的I帧
    uint16_t start_seq_num = frame->first_seq_num();
    RTC_LOG(LS_WARNING) << "received H.264-IDR frame, seq_range[" << start_seq_num
        << ", " << frame->last_seq_num() << "], video_seq_: " << video_seq_
        << ", ahead: " << (webrtc::AheadOf(start_seq_num, video_seq_) ? "true" : "false");

    // 最新的I帧第一个包的序列号大于当前收到的最大的连续的序列号
    if (webrtc::AheadOf(start_seq_num, video_seq_)) {
        // 从最新的I帧位置开始发送数据
        int test_size = 0;
        uint16_t cur_seq = start_seq_num;
        uint16_t last_fwd = start_seq_num;
        do {
            test_size++;

            auto video_packet = FindVideoPacket(cur_seq);
            if (!video_packet) {
                RTC_LOG(LS_WARNING) << "IDR restart forward break at seq: " << cur_seq
                    << ", forwarded: " << (int)(cur_seq - start_seq_num) << " packets";
                break;
            }

            if (listener_) {
                listener_->OnRtpPacket(this, webrtc::MediaType::VIDEO, video_packet);
            }
            last_fwd = cur_seq;

            ++cur_seq;

        } while (test_size <= kVideoPacketCacheSize);
        RTC_LOG(LS_WARNING) << "IDR restart done, last_fwd: " << last_fwd;

        // 清除这个[video_seq_ + 1, start_seq_num - 1]范围的缓存，这样可以避免重传
        // 防止拉流端的过时重传请求找到缓存包（非常重要）
        ClearVideoPacketCache(video_seq_ + 1, start_seq_num - 1);

        video_seq_ = cur_seq - 1;
    }

}

void PushStream::ClearVideoPacketCache(uint16_t start_seq, uint16_t end_seq) {
    int test_size = 0;
    while (webrtc::AheadOrAt(end_seq, start_seq) && test_size <= kVideoPacketCacheSize) {
        int index = start_seq % kVideoPacketCacheSize;
        video_data_buffer_[index] = nullptr;
        ++start_seq;
        ++test_size;
    }
}

} // namespace xrtc


