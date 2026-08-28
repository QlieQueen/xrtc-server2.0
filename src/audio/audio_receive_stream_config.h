#ifndef XRTCSERVER_AUDIO_RECEIVE_STREAM_CONFIG_H_
#define XRTCSERVER_AUDIO_RECEIVE_STREAM_CONFIG_H_

#include <system_wrappers/include/clock.h>

#include "base/event_loop.h"

namespace xrtc {

class AudioReceiveStreamConfig {
public:
    EventLoop* el = nullptr;
    webrtc::Clock* clock = nullptr;
};

} // namespace xrtc

#endif // XRTCSERVER_AUDIO_RECEIVE_STREAM_CONFIG_H_
