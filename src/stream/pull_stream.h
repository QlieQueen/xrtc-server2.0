/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file pull_stream.h
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/



#ifndef  __XRTCSERVER_STREAM_PULL_STREAM_H_
#define  __XRTCSERVER_STREAM_PULL_STREAM_H_

#include "stream/rtc_stream.h"

namespace xrtc {

class PullStream : public RtcStream {
public:
    PullStream(EventLoop* el, PortAllocator* allocator, uint64_t uid, 
            const std::string& stream_name,
            bool audio, bool video, uint32_t log_id);
    ~PullStream() override;

    std::string CreateOffer() override;
    RtcStreamType stream_type() override { return RtcStreamType::kPull; }

    void SetSrInfo(webrtc::MediaType media_type, uint32_t rtp_timestamp,
            webrtc::NtpTime ntp);
    int SendPacket(webrtc::MediaType media_type, const uint8_t* buf,
            size_t len);

    void AddAudioSource(const std::vector<StreamParams>& source);
    void AddVideoSource(const std::vector<StreamParams>& source);

private:
    void OnNackReceived(PeerConnection*, webrtc::MediaType media_type,
            const std::vector<uint16_t>& nack_list);
};

} // namespace xrtc

#endif  //__XRTCSERVER_STREAM_PULL_STREAM_H_


