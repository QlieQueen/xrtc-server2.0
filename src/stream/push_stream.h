/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file push_stream.h
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/



#ifndef  __XRTCSERVER_STREAM_PUSH_STREAM_H_
#define  __XRTCSERVER_STREAM_PUSH_STREAM_H_

#include <memory>
#include <vector>

#include "stream/rtc_stream.h"
#include "stream/rtc_packet.h"

namespace xrtc {

namespace {

const int kVideoPacketCacheSize = 2048;

} // namespace


class PushStream : public RtcStream {
public:
    PushStream(EventLoop* el, PortAllocator* allocator, uint64_t uid, 
            const std::string& stream_name,
            bool audio, bool video, uint32_t log_id);
    ~PushStream() override;

    std::string CreateOffer() override;
    RtcStreamType stream_type() override { return RtcStreamType::kPush; }
    void set_pli(bool is_pli) { pc->set_pli(is_pli); }

    std::shared_ptr<RtcPacket> FindVideoPacket(uint16_t seq_num);

    bool GetAudioSource(std::vector<StreamParams>& source);
    bool GetVideoSource(std::vector<StreamParams>& source);

private:
    void OnRtpPacket(PeerConnection*, webrtc::MediaType media_type,
            const webrtc::RtpPacketReceived& packet);
    void CacheVideoPacket(std::shared_ptr<RtcPacket> packet);

    bool GetSource(const std::string& mid, std::vector<StreamParams>& source);

private:
    std::vector<std::shared_ptr<RtcPacket>> video_data_buffer_;
};

} // namespace xrtc

#endif  //__XRTCSERVER_STREAM_PUSH_STREAM_H_


