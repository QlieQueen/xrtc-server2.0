#include "modules/rtp_rtcp/rtp_rtcp_impl.h"

#include <rtc_base/logging.h>

#include "base/conf.h"

extern xrtc::GeneralConf* g_conf;

namespace xrtc {

namespace {

// RTCP周期定时器的回调: 定时到期即触发一次周期上报
void RtcpReportCb(EventLoop* /*el*/, TimerWatcher* /*w*/, void* data) {
    RtpRtcpImpl* rtp_rtcp = (RtpRtcpImpl*)data;
    rtp_rtcp->TimeToSendRTCP();
}

} // namespace


RtpRtcpImpl::RtpRtcpImpl(const RtpRtcpConfig& config) :
    el_(config.el),
    rtcp_sender_(config),
    rtcp_receiver_(config)
{

}

RtpRtcpImpl::~RtpRtcpImpl() {
    // 析构时删除周期定时器, 避免回调悬垂
    if (rtcp_report_timer_) {
        el_->DeleteTimer(rtcp_report_timer_);
        rtcp_report_timer_ = nullptr;
    }
}

// 周期上报触发点: 以kRtcpReport类型发送RTCP,
// RTCPSender内部由PrepareReport按发送模式决定本次是否附带SR/RR
void RtpRtcpImpl::TimeToSendRTCP() {
    rtcp_sender_.SendRTCP(GetFeedbackState(), webrtc::kRtcpReport);
    uint32_t interval = rtcp_sender_.cur_report_interval_ms();
    RTC_LOG(LS_WARNING) << "==============cur report interval ms: " << interval;
    el_->StartTimer(rtcp_report_timer_, interval * 1000);
}

void RtpRtcpImpl::SendNack(const std::vector<uint16_t>& nack_list) {
    rtcp_sender_.SendRTCP(GetFeedbackState(), webrtc::kRtcpNack,
            nack_list.size(), nack_list.data());
}

// 设置RTCP开关: 开启时创建周期定时器(按conf配置间隔触发上报), 关闭时删除
void RtpRtcpImpl::SetRtcpStatus(webrtc::RtcpMode method) {
    if (method == webrtc::RtcpMode::kOff) {
        // 关闭RTCP: 删除定时器, 停止周期上报
        if (rtcp_report_timer_) {
            el_->DeleteTimer(rtcp_report_timer_);
            rtcp_report_timer_ = nullptr;
        }
    } else {
        // 开启RTCP: 定时器不存在才创建(重复开启不重复创建);
        // interval单位ms, EventLoop的StartTimer单位us, 需乘以1000
        if (!rtcp_report_timer_) {
            rtcp_report_timer_ = el_->CreateTimer(RtcpReportCb, this, true);
            el_->StartTimer(rtcp_report_timer_, g_conf->rtcp_report_timer_interval * 1000);
        }
    }

    // 底层RTCPSender同步设置发送模式
    rtcp_sender_.SetRtcpStatus(method);
}

// 收到RTCP数据: 交给RTCPReceiver拆包解析(复合包解包+SR/RR分派)
void RtpRtcpImpl::IncomingRtcpPacket(const uint8_t* data, size_t len) {
    rtcp_receiver_.IncomingRtcpPacket(data, len);
}

// 设置远端媒体流SSRC: RTCPReceiver 过滤SR包时用
void RtpRtcpImpl::SetRemoteSsrc(uint32_t ssrc) {
    rtcp_receiver_.SetRemoteSsrc(ssrc);
}

RTCPSender::FeedbackState RtpRtcpImpl::GetFeedbackState() {
    RTCPSender::FeedbackState state;

    uint32_t receive_ntp_secs;
    uint32_t receive_ntp_frac;
    state.remote_sr = 0;

    if (rtcp_receiver_.NTP(&receive_ntp_secs,
                           &receive_ntp_frac,
                           &state.last_rr_ntp_secs,
                           &state.last_rr_ntp_frac,
                           nullptr))
    {
        state.remote_sr = ((receive_ntp_secs & 0x0000FFFF) << 16) + 
                    ((receive_ntp_frac & 0xFFFF0000) >> 16);
    }

    return state;
}

} // namespace xrtc
