#include "video/rtp_video_stream_receiver.h"

#include <rtc_base/logging.h>

namespace xrtc {

namespace {

const int kPacketBufferStartSize = 512;
const int kPacketBufferMaxSize = 2048;

std::unique_ptr<RtpRtcpImpl> CreateRtpRtcpModule(
        const VideoReceiveStreamConfig& vconf,
        ReceiveStat* receive_stat)
{
    RtpRtcpConfig config;
    config.el = vconf.el;
    config.clock = vconf.clock;
    config.local_media_ssrc = vconf.rtp.local_ssrc;
    // 把 VideoReceiveStream 持有的 ReceiveStat 传给 RTCP 模块,
    // 使 RTCPSender 能拿到接收统计构建 RR 报告块
    config.receive_stat = receive_stat;
    config.rtp_rtcp_module_observer = vconf.rtp_rtcp_module_observer;

    auto rtp_rtcp = std::make_unique<RtpRtcpImpl>(config);
    rtp_rtcp->SetRtcpStatus(webrtc::RtcpMode::kCompound);
    return rtp_rtcp;
}

} // namespace

RtpVideoStreamReceiver::RtpVideoStreamReceiver(const VideoReceiveStreamConfig& config,
        ReceiveStat* rtp_receive_stat) :
    config_(config),
    rtp_receive_stat_(rtp_receive_stat),
    rtp_rtcp_(CreateRtpRtcpModule(config, rtp_receive_stat)),
    video_rtp_depacketizer_(std::make_unique<webrtc::VideoRtpDepacketizerH264>()),
    // 乱序缓存 + 组帧器(环形vector, 按 seq % size 映射槽位):
    // 起始 512 槽位, 冲突时动态翻倍扩容, 上限 2048(均为2的幂)
    // InsertPacket 内部识别完整帧("两触发三闸门", 见 v2_5.2 笔记)
    packet_buffer_(std::make_unique<webrtc::video_coding::PacketBuffer>(
                kPacketBufferStartSize, kPacketBufferMaxSize)),
    nack_module_(std::make_unique<NackRequester>(config.clock, config.el))
{
    // 把远端媒体流SSRC交给RTCP模块: RTCPReceiver解析SR时过滤用
    rtp_rtcp_->SetRemoteSsrc(config.rtp.remote_ssrc);
    nack_module_->SignalNackSend.connect(this, &RtpVideoStreamReceiver::OnNackSend);
}

RtpVideoStreamReceiver::~RtpVideoStreamReceiver() {

}

void RtpVideoStreamReceiver::OnRtpPacket(const webrtc::RtpPacketReceived& packet) {
    if (config_.rtp_rtcp_module_observer) {
        // 包含正常的包和rtx重传包
        config_.rtp_rtcp_module_observer->OnRtpPacket(webrtc::MediaType::VIDEO,
                packet);
    }
    
    ReceivePacket(packet);

    // recovered() = 由 RTX/重传恢复出的包. 接收统计只应反映"真实到达"的包,
    // 重传恢复的包若不排除, 会歪曲丢包率/jitter 的计算
    if (!packet.recovered()) {
        rtp_receive_stat_->OnRtpPacket(packet);
    }
}

void RtpVideoStreamReceiver::ReceivePacket(const webrtc::RtpPacketReceived& packet) {
    if (packet.payload_size() == 0) {
        return;
    }

    // Parse 输入 = RTP 载荷(含打包格式头: FU-A/STAP-A/generic头)
    // 输出 ParsedRtpPayload = video_payload(剥离打包格式后的纯编码数据) + video_header(元数据)
    absl::optional<webrtc::VideoRtpDepacketizer::ParsedRtpPayload> parsed_payload =
        video_rtp_depacketizer_->Parse(packet.PayloadBuffer());
    if (absl::nullopt == parsed_payload) {
        RTC_LOG(LS_WARNING) << "parsing rtp payload failed";
        return;
    }

    OnReceivedPayloadData(std::move(parsed_payload->video_payload),
            packet, parsed_payload->video_header);
}

void RtpVideoStreamReceiver::OnReceivedPayloadData(
        rtc::CopyOnWriteBuffer codec_payload,
        const webrtc::RtpPacketReceived& rtp_packet,
        const webrtc::RTPVideoHeader& video)
{
    // 把"线上包"变成"缓冲单元": Packet 只拷 RTP 头字段 + video_header,
    // video_payload 不填 —— SFU 不拼帧不解码, 转发的是 RTP 包(7.x)
    auto packet = std::make_unique<webrtc::video_coding::PacketBuffer::Packet>(
            rtp_packet, video);

    // H264 的 depacketizer 不填帧尾标记(E 位是 NAL 级边界, 不是帧级),
    // 帧尾必须靠 RTP 头 M 位补 —— FindFrames 触发组帧的依据
    webrtc::RTPVideoHeader& video_header = packet->video_header;
    video_header.is_last_packet_in_frame |= rtp_packet.Marker();

    if (nack_module_) {
        bool is_keyframe = video_header.is_first_packet_in_frame &&
            video_header.frame_type == webrtc::VideoFrameType::kVideoFrameKey;
        // is_retransmitted 传 recovered(): 恢复包(重传补上的)不进乱序直方图,
        // 只有真乱序包才统计 —— 避免恢复包污染 NACK 等待线
        nack_module_->OnReceivedPacket(rtp_packet.SequenceNumber(), is_keyframe,
                rtp_packet.recovered());
    }

    // 进环形缓冲组帧; 结果(完整帧的包集合 / 缓冲被清)交给 OnInsertedPacket
    OnInsertedPacket(packet_buffer_->InsertPacket(std::move(packet)));
}

// 插包结果处理: 当前为空体
// result.packets = 完整帧的包集合(按序, 可能含连续多帧, 帧边界靠首包标记切)
// result.buffer_cleared = 缓冲被清空, 应请求关键帧(PLI, 8.x 用上)
// 5.5 在这里切帧: 遍历 packets, 首包标记记起点, 末包标记构 RtpFrameObject
void RtpVideoStreamReceiver::OnInsertedPacket(
        webrtc::video_coding::PacketBuffer::InsertResult result)
{
    if (result.packets.size() <= 0) {
        return;
    }

    webrtc::video_coding::PacketBuffer::Packet* first_packet = nullptr;

    for (auto& packet : result.packets) {
        if (packet->is_first_packet_in_frame()) {
            first_packet = packet.get();
        }

        if (packet->is_last_packet_in_frame()) {
            webrtc::video_coding::PacketBuffer::Packet* last_packet 
                                                            = packet.get();
            OnAssembledFrame(std::make_unique<RtpFrameObject>(
                        first_packet->seq_num,
                        last_packet->seq_num,
                        first_packet->codec(),
                        first_packet->video_header));
        }
    }
}

// 一个完整帧组装完成
void RtpVideoStreamReceiver::OnAssembledFrame(std::unique_ptr<RtpFrameObject> frame) {
    if (config_.rtp_rtcp_module_observer) {
        config_.rtp_rtcp_module_observer->OnFrame(std::move(frame));
    }
}

void RtpVideoStreamReceiver::OnNackSend(const std::vector<uint16_t>& nack_list) {
    std::stringstream ss;
    for (auto seq_num : nack_list) {
        ss << seq_num << ", ";
    }
    RTC_LOG(LS_WARNING) << "============nack list: " << ss.str();

   rtp_rtcp_->SendNack(nack_list);
}

// 收到RTCP数据: 转给RTCP模块(RtpRtcpImpl), 由RTCPReceiver拆包解析
void RtpVideoStreamReceiver::DeliverRtcp(const uint8_t* data, size_t len) {
    rtp_rtcp_->IncomingRtcpPacket(data, len);
}

} // namespace xrtc
