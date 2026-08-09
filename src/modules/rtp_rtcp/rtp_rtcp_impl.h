#ifndef __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_
#define __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_

#include "modules/rtp_rtcp/rtp_rtcp_config.h"
#include "modules/rtp_rtcp/rtcp_sender.h"

namespace xrtc {

// RTCP模块封装: 组合RTCPSender, 提供周期定时上报
class RtpRtcpImpl {
public:
    RtpRtcpImpl(const RtpRtcpConfig& config);
    ~RtpRtcpImpl();

    // 设置RTCP开关: 开启时创建周期定时器, 关闭时删除
    void SetRtcpStatus(webrtc::RtcpMode method);
    // 周期上报触发点(定时器回调调用), 以kRtcpReport类型发送
    void TimeToSendRTCP();

private:
    EventLoop* el_;
    RTCPSender rtcp_sender_;

    // RTCP周期上报定时器, 按conf的rtcp_report_timer_interval创建
    TimerWatcher* rtcp_report_timer_ = nullptr;
};

} // namespace xrtc

#endif // __XRTCSERVER_MODULES_RTP_RTCP_RTP_RTCP_IMPL_H_
