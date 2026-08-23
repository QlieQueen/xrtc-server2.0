#ifndef XRTCSERVER_RTC_PACKET_H_
#define XRTCSERVER_RTC_PACKET_H_

#include <cstdint>
#include <cstring>
#include <api/media_types.h>

namespace xrtc {

class RtcPacket {
public:
    RtcPacket(uint16_t seq_num, webrtc::MediaType media_type,
            bool is_keyframe, int64_t ts,
            uint8_t* buf, size_t len) :
        seq_num(seq_num), media_type(media_type),
        is_keyframe(is_keyframe), ts(ts),
        len(len)
    {
        this->buf = new uint8_t[len];
        memcpy(this->buf, buf, len);
    }
    
    ~RtcPacket() {
        if (buf) {
            delete []buf;
            buf = nullptr;
        }

        len = 0;
    }

    uint16_t seq_num;
    webrtc::MediaType media_type;
    bool is_keyframe;
    int64_t ts;
    uint8_t* buf;
    size_t len;
};

} // namespace xrtc

#endif // XRTCSERVER_RTC_PACKET_H_
