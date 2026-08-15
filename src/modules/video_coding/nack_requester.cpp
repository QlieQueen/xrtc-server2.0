#include "modules/video_coding/nack_requester.h"

#include <rtc_base/logging.h>

namespace xrtc {

NackRequester::NackRequester(webrtc::Clock* clock) :
    clock_(clock)
{

}

NackRequester::~NackRequester() {

}

int NackRequester::OnReceivedPacket(uint16_t seq_num) {
    RTC_LOG(LS_WARNING) << "=============seq_num: " << seq_num;
    return 0;
}

} // namespace xrtc
