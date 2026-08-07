/***************************************************************************
 * 
 * Copyright (c) str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file rtp_utils.h
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/



#ifndef  __XRTCSERVER_MODULES_RTP_RTCP_RTP_UTILS_H_
#define  __XRTCSERVER_MODULES_RTP_RTCP_RTP_UTILS_H_

#include <api/array_view.h>

namespace xrtc {

enum class RtpPacketType {
    kRtp,
    kRtcp,
    kUnknown,
};

RtpPacketType InferRtpPacketType(rtc::ArrayView<const char> packet);

uint16_t ParseRtpSequenceNumber(rtc::ArrayView<const uint8_t> packet);
uint32_t ParseRtpSsrc(rtc::ArrayView<const uint8_t> packet);
bool GetRtcpType(const void* data, size_t len, int* type);

} // namespace xrtc

#endif  //__XRTCSERVER_MODULES_RTP_RTCP_RTP_UTILS_H_


