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

    CacheVideoPacket(new_packet);

    if (listener_) {
        listener_->OnRtpPacket(this, media_type, packet);
    }
}

} // namespace xrtc


