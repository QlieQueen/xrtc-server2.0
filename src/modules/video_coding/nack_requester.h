#ifndef XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_
#define XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_

#include <system_wrappers/include/clock.h>

namespace xrtc {

class NackRequester {
public:
    NackRequester(webrtc::Clock* clock);
    ~NackRequester();

    // 返回某序号包的重传次数
    int OnReceivedPacket(uint16_t seq_num);

private:
    webrtc::Clock* clock_;
};

} // namespace xrtc

#endif // XRTCSERVER_MODULES_VIDEO_CODING_NACK_REQUESTER_H_