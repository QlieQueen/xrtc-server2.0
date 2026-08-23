/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file pull_stream.cpp
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/

#include "stream/pull_stream.h"

#include <rtc_base/logging.h>

namespace xrtc {

PullStream::PullStream(EventLoop* el, PortAllocator* allocator, 
        uint64_t uid, const std::string& stream_name,
        bool audio, bool video, uint32_t log_id) :
    RtcStream(el, allocator, uid, stream_name, audio, video, log_id)
{
}

PullStream::~PullStream() {
    RTC_LOG(LS_INFO) << ToString() << ": Pull stream destroy.";
}

std::string PullStream::CreateOffer() {
    RTCOfferAnswerOptions options;
    options.send_audio = audio;
    options.send_video = video;
    options.recv_audio = false;
    options.recv_video = false;

    return pc->CreateOffer(options);
}

void PullStream::AddAudioSource(const std::vector<StreamParams>& source) {
    if (pc) {
        pc->AddAudioSource(source);
    }
}

void PullStream::AddVideoSource(const std::vector<StreamParams>& source) {
    if (pc) {
        pc->AddVideoSource(source);
    }
}

int PullStream::SendPacket(webrtc::MediaType media_type, const uint8_t* buf,
        size_t len)
{
    if (!pc && state() != PeerConnectionState::kConnected) {
        return -1;
    }

    webrtc::RtpPacketToSend send_packet(nullptr);
    if (!send_packet.Parse(buf, len)) {
        return -1;
    }

    return pc->SendPacket(media_type, send_packet);
}

} // namespace xrtc


