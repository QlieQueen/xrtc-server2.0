/***************************************************************************
 * 
 * Copyright (c) 2022 str2num.com, Inc. All Rights Reserved
 * $Id$ 
 * 
 **************************************************************************/
 
 
 
/**
 * @file peer_connection.cpp
 * @author str2num
 * @version $Revision$ 
 * @brief 
 *  
 **/

#include "pc/peer_connection.h"

#include <absl/algorithm/container.h>
#include <rtc_base/logging.h>
#include <modules/rtp_rtcp/source/rtp_packet_received.h>

#include "ice/ice_credentials.h"

namespace xrtc {

namespace {

const uint32_t kDefaultVideoSsrc = 1;  // RR包中的 SSRC of packet Sender

} // namespace

struct SsrcInfo {
    uint32_t ssrc_id;
    std::string cname;
    std::string stream_id;
    std::string track_id;
};

static RtpDirection GetDirection(bool send, bool recv) {
    if (send && recv) {
        return RtpDirection::kSendRecv;
    } else if (send && !recv) {
        return RtpDirection::kSendOnly;
    } else if (!send && recv) {
        return RtpDirection::kRecvOnly;
    } else {
        return RtpDirection::kInactive;
    }
}

PeerConnection::PeerConnection(EventLoop* el, PortAllocator* allocator) : 
    el_(el),
    clock_(webrtc::Clock::GetRealTimeClock()),
    transport_controller_(new TransportController(el, allocator))
{
    transport_controller_->SignalCandidateAllocateDone.connect(this,
            &PeerConnection::OnCandidateAllocateDone);
    transport_controller_->SignalConnectionState.connect(this,
            &PeerConnection::OnConnectionState);
    transport_controller_->SignalRtpPacketReceived.connect(this,
            &PeerConnection::OnRtpPacketReceived);
    transport_controller_->SignalRtcpPacketReceived.connect(this,
            &PeerConnection::OnRtcpPacketReceived);
}

PeerConnection::~PeerConnection() {
    if (destroy_timer_) {
        el_->DeleteTimer(destroy_timer_);
        destroy_timer_ = nullptr;
    }
    
    RTC_LOG(LS_INFO) << "PeerConnection destroy";
}

void PeerConnection::OnCandidateAllocateDone(TransportController* /*transport_controller*/,
        const std::string& transport_name,
        IceCandidateComponent /*component*/,
        const std::vector<Candidate>& candidates)
{
    for (auto c : candidates) {
        RTC_LOG(LS_INFO) << "candidate gathered, transport_name: " << transport_name
            << ", " << c.ToString();
    }

    if (!local_desc_) {
         return;   
    }

    auto content = local_desc_->GetContent(transport_name);
    if (content) {
        content->add_candidates(candidates);
    }
}

void PeerConnection::OnConnectionState(TransportController*, PeerConnectionState state) {
    SignalConnectionState(this, state);
}

webrtc::MediaType PeerConnection::GetMediaType(uint32_t ssrc) const {
    if (ssrc == remote_video_ssrc_ || ssrc == remote_video_rtx_ssrc_) {
        return webrtc::MediaType::VIDEO;
    } else if (ssrc == remote_audio_ssrc_){
        return webrtc::MediaType::AUDIO;
    } else {
        return webrtc::MediaType::ANY;
    }
}

void PeerConnection::OnRtpPacketReceived(TransportController*,
        rtc::CopyOnWriteBuffer* packet, int64_t ts)
{
    // 1.把RTP数据塞进RtpPacketReceived 对象，解析出 SSRC/序列号/时间戳等字段
    webrtc::RtpPacketReceived parsed_packet;
    if (!parsed_packet.Parse(std::move(*packet))) {   // move：字节所有权转移给对象
        RTC_LOG(LS_WARNING) << "invalid rtp packet";
        return;
    }

    // 2.记录 包的到达时间 -- 将来算 jitter 的时间基准
    if (ts > 0) {
        parsed_packet.set_arrival_time(webrtc::Timestamp::Micros(ts));
    } else {
        parsed_packet.set_arrival_time(clock_->CurrentTime());
    }

    // 3. 用 SSRC 判断是：视频/音频/RTX
    webrtc::MediaType packet_type = GetMediaType(parsed_packet.Ssrc());
    if (packet_type == webrtc::MediaType::VIDEO) {
        parsed_packet.set_payload_type_frequency(webrtc::kVideoPayloadTypeFrequency);
        if (video_receive_stream_) {
            video_receive_stream_->OnRtpPacket(parsed_packet);
        }
    }

    //SignalRtpPacketReceived(this, packet, ts);
}

void PeerConnection::OnRtcpPacketReceived(TransportController*,
        rtc::CopyOnWriteBuffer* packet, int64_t ts)
{
    // 收到的RTCP包直接投递给视频接收流, 由RTCPReceiver解析(4.13起走这条链路);
    // 原SignalRtcpPacketReceived信号链路暂保留
    if (video_receive_stream_) {
        video_receive_stream_->DeliverRtcp(packet->data(), packet->size());
    }

    //SignalRtcpPacketReceived(this, packet, ts);
}

int PeerConnection::Init(rtc::RTCCertificate* certificate) {
    if (certificate) {
        certificate_ = certificate;
        transport_controller_->SetLocalCertificate(certificate);
    } else { // 不开启DTLS
        is_dtls_ = false;
        transport_controller_->set_dtls(is_dtls_);
    }

    return 0;
}

void DestroyTimerCb(EventLoop* /*el*/, TimerWatcher* /*w*/, void* data) {
    PeerConnection* pc = (PeerConnection*)data;
    delete pc;
}

void PeerConnection::Destroy() {
    if (destroy_timer_) {
        el_->DeleteTimer(destroy_timer_);
        destroy_timer_ = nullptr;
    }

    destroy_timer_ = el_->CreateTimer(DestroyTimerCb, this, false);
    el_->StartTimer(destroy_timer_, 10000); // 10ms
}

std::string PeerConnection::CreateOffer(const RTCOfferAnswerOptions& options) {
    if (is_dtls_ && !certificate_) {
        RTC_LOG(LS_WARNING) << "certificate is null";
        return "";
    }

    local_desc_ = std::make_unique<SessionDescription>(SdpType::kOffer);
    
    IceParameters ice_param = IceCredentials::CreateRandomIceCredentials();

    if (options.recv_audio || options.send_audio) {
        auto audio = std::make_shared<AudioContentDescription>();
        audio->set_direction(GetDirection(options.send_audio, options.recv_audio));
        audio->set_rtcp_mux(options.use_rtcp_mux);
        local_desc_->AddContent(audio);
        local_desc_->AddTransportInfo(audio->mid(), ice_param, certificate_);

        if (options.send_audio) {
            for (auto stream : audio_source_) {
                audio->add_stream(stream);
            }
        }
    }

    if (options.recv_video || options.send_video) {
        auto video = std::make_shared<VideoContentDescription>();
        video->set_direction(GetDirection(options.send_video, options.recv_video));
        video->set_rtcp_mux(options.use_rtcp_mux);
        local_desc_->AddContent(video);
        local_desc_->AddTransportInfo(video->mid(), ice_param, certificate_);

        if (options.send_video) {
            for (auto stream : video_source_) {
                video->add_stream(stream);
            }
        }
    }
    
    if (options.use_rtp_mux) {
        ContentGroup offer_bundle("BUNDLE");
        for (auto content : local_desc_->contents()) {
            offer_bundle.AddContentName(content->mid());
        }

        if (!offer_bundle.content_names().empty()) {
            local_desc_->AddGroup(offer_bundle);
        }
    }
    
    transport_controller_->SetLocalDescription(local_desc_.get());

    return local_desc_->ToString();
}

static std::string GetAttribute(const std::string& line) {
    std::vector<std::string> fields;
    size_t size = rtc::tokenize(line, ':', &fields);
    if (size != 2) {
        RTC_LOG(LS_WARNING) << "get attribute error: " << line;
        return "";
    }
 
    return fields[1];
}

static int ParseTransportInfo(TransportDescription* td, const std::string& line) {
    if (line.find("a=ice-ufrag") != std::string::npos) {
        td->ice_ufrag = GetAttribute(line);
        if (td->ice_ufrag.empty()) {
            return -1;
        }
    } else if (line.find("a=ice-pwd") != std::string::npos) {
        td->ice_pwd = GetAttribute(line);
        if (td->ice_pwd.empty()) {
            return -1;
        }
    } else if (line.find("a=fingerprint") != std::string::npos) {
        std::vector<std::string> items;
        rtc::tokenize(line, ' ', &items);
        if (items.size() != 2) {
            RTC_LOG(LS_WARNING) << "parse a=fingerprint error: " << line;
            return -1;
        }

        // a=fingerprint: 14
        std::string alg = items[0].substr(14);
        absl::c_transform(alg, alg.begin(), ::tolower);
        std::string content = items[1];
              
        td->identity_fingerprint = rtc::SSLFingerprint::CreateUniqueFromRfc4572(
                alg, content);
        if (!(td->identity_fingerprint.get())) {
            RTC_LOG(LS_WARNING) << "create fingerprint error: " << line;
            return -1;
        }
    }

    return 0;
}

static int ParseSsrcInfo(std::vector<SsrcInfo>& ssrc_info, const std::string& line) {
    if (line.find("a=ssrc:") == std::string::npos) {
        return 0;
    }
    
    //rfc5576
    // a=ssrc:<ssrc-id> <attribute>
    // a=ssrc:<ssrc-id> <attribute>:<value>
    std::string field1, field2;
    if (!rtc::tokenize_first(line.substr(2), ' ', &field1, &field2)) {
        RTC_LOG(LS_WARNING) << "parse a=ssrc failed, line: " << line;
        return -1;
    }

    // ssrc:<ssrc-id>
    std::string ssrc_id_s = field1.substr(5);
    uint32_t ssrc_id = 0;
    if (!rtc::FromString(ssrc_id_s, &ssrc_id)) {
        RTC_LOG(LS_WARNING) << "invalid ssrc_id, line: " << line;
        return -1;
    }
    
    // <attribute>
    std::string attribute;
    std::string value;
    if (!rtc::tokenize_first(field2, ':', &attribute, &value)) {
        RTC_LOG(LS_WARNING) << "get ssrc attribute failed, line: " << line;
        return -1;
    }
    
    auto iter = ssrc_info.begin();
    for (; iter != ssrc_info.end(); ++iter) {
        if (iter->ssrc_id == ssrc_id) {
            break;
        }
    }
    
    if (iter == ssrc_info.end()) {
        SsrcInfo info;
        info.ssrc_id = ssrc_id;
        ssrc_info.push_back(info);
        iter = ssrc_info.end() - 1;
    }
    
    if ("cname" == attribute) {
        iter->cname = value;
    } else if ("msid" == attribute) {
        std::vector<std::string> fields;
        rtc::split(value, ' ', &fields);
        if (fields.size() < 1 || fields.size() > 2) {
            RTC_LOG(LS_WARNING) << "msid format error, line: " << line;
            return -1;
        }

        iter->stream_id = fields[0];
        if (fields.size() == 2) {
            iter->track_id = fields[1];
        }
    }

    return 0;
}

static int ParseSsrcGroupInfo(std::vector<SsrcGroup>& ssrc_groups,
        const std::string& line)
{
    if (line.find("a=ssrc-group:") == std::string::npos) {
        return 0;
    }

    // rfc5576
    // a=ssrc-group:<semantics> <ssrc-id> ...
    std::vector<std::string> fields;
    rtc::split(line.substr(2), ' ', &fields);
    if (fields.size() < 2) {
        RTC_LOG(LS_WARNING) << "ssrc-group field size < 2, line: " << line;
        return -1;
    }

    std::string semantics = GetAttribute(fields[0]);
    if (semantics.empty()) {
        return -1;
    }

    std::vector<uint32_t> ssrcs;
    for (size_t i = 1; i < fields.size(); ++i) {
        uint32_t ssrc_id = 0;
        if (!rtc::FromString(fields[i], &ssrc_id)) {
            return -1;
        }
        ssrcs.push_back(ssrc_id);
    }

    ssrc_groups.push_back(SsrcGroup(semantics, ssrcs));

    return 0;
}

static void CreateTrackFromSsrcInfo(const std::vector<SsrcInfo>& ssrc_infos,
        std::vector<StreamParams>& tracks)
{
    for (auto ssrc_info : ssrc_infos) {
        std::string track_id = ssrc_info.track_id;
        
        auto iter = tracks.begin();
        for (; iter != tracks.end(); ++iter) {
            if (iter->id == track_id) {
                break;
            }
        }

        if (iter == tracks.end()) {
            StreamParams track;
            track.id = track_id;
            tracks.push_back(track);
            iter = tracks.end() - 1;
        }

        iter->cname = ssrc_info.cname;
        iter->stream_id = ssrc_info.stream_id;
        iter->ssrcs.push_back(ssrc_info.ssrc_id);
    }
}

int PeerConnection::SetRemoteSdp(const std::string& sdp) {
    std::vector<std::string> fields;
    size_t size = rtc::tokenize(sdp, '\n', &fields);
    if (size <= 0) {
        RTC_LOG(LS_WARNING) << "sdp invalid";
        return -1;
    }
    
    bool is_rn = false;
    if (sdp.find("\r\n") != std::string::npos) {
        is_rn = true;
    }
    
    remote_desc_ = std::make_unique<SessionDescription>(SdpType::kAnswer);

    std::string media_type;
   
    auto audio_content = std::make_shared<AudioContentDescription>();
    auto video_content = std::make_shared<VideoContentDescription>();
    auto audio_td = std::make_shared<TransportDescription>();
    auto video_td = std::make_shared<TransportDescription>();
    std::vector<SsrcInfo> audio_ssrc_info;
    std::vector<SsrcInfo> video_ssrc_info;
    std::vector<SsrcGroup> video_ssrc_groups;
    std::vector<StreamParams> audio_tracks;
    std::vector<StreamParams> video_tracks;

    for (auto field : fields) {
        if (is_rn) {
            field = field.substr(0, field.length() - 1);
        }

        if (field.find("m=group:BUNDLE") != std::string::npos) {
            std::vector<std::string> items;
            rtc::split(field, ' ', &items);
            if (items.size() > 1) {
                ContentGroup answer_bundle("BUNDLE");
                for (size_t i = 1; i < items.size(); ++i) {
                    answer_bundle.AddContentName(items[i]);
                }
                remote_desc_->AddGroup(answer_bundle);
            }
        } else if (field.find("m=") != std::string::npos) {
            std::vector<std::string> items;
            rtc::split(field, ' ', &items);
            if (items.size() <= 2) {
                RTC_LOG(LS_WARNING) << "parse m= error: " << field;
                return -1;
            }

            // m=audio/video
            media_type = items[0].substr(2);
            if ("audio" == media_type) {
                remote_desc_->AddContent(audio_content);
                audio_td->mid = "audio";
            } else if ("video" == media_type){
                remote_desc_->AddContent(video_content);
                video_td->mid = "video";
            }
        }

        if ("audio" == media_type) {
            if (ParseTransportInfo(audio_td.get(), field) != 0) {
                return -1;
            }
            
            if (ParseSsrcInfo(audio_ssrc_info, field) != 0) {
                return -1;
            }

        } else if ("video" == media_type) {
            if (ParseTransportInfo(video_td.get(), field) != 0) {
                return -1;
            }
            
            if (ParseSsrcGroupInfo(video_ssrc_groups, field) != 0) {
                return -1;
            }

            if (ParseSsrcInfo(video_ssrc_info, field) != 0) {
                return -1;
            }
        }
    }
    
    if (!audio_ssrc_info.empty()) {
        CreateTrackFromSsrcInfo(audio_ssrc_info, audio_tracks);

        for (auto track : audio_tracks) {
            audio_content->add_stream(track);
        }
    }

    if (!video_ssrc_info.empty()) {
        CreateTrackFromSsrcInfo(video_ssrc_info, video_tracks);

        for (auto ssrc_group : video_ssrc_groups) {
            if (ssrc_group.ssrcs.empty()) {
                continue;
            }
            
            uint32_t ssrc = ssrc_group.ssrcs.front();
            for (StreamParams& track : video_tracks) {
                if (track.HasSsrc(ssrc)) {
                    track.ssrc_groups.push_back(ssrc_group);
                }
            }
        }

        for (auto track : video_tracks) {
            video_content->add_stream(track);
        }
    }

    remote_desc_->AddTransportInfo(audio_td);
    remote_desc_->AddTransportInfo(video_td);

    CreateVideoReceiveStream(video_content.get());
   
    transport_controller_->SetRemoteDescription(remote_desc_.get());
    return 0;
}

void PeerConnection::CreateVideoReceiveStream(VideoContentDescription* video_content) {
    // 按照系统的推拉流原子设计原则，一个peerconnection只允许推一个或者拉一个视频
    for (auto send_stream : video_content->streams()) {
        uint32_t ssrc = send_stream.FirstSsrc();
        if (ssrc != 0) {
            VideoReceiveStreamConfig config;
            remote_video_ssrc_ = ssrc;  // 视频主 SSRC
            if (send_stream.ssrcs.size() >= 2) {
                remote_video_rtx_ssrc_ = send_stream.ssrcs[1];  // RTX SSRC
            }

            config.el = el_;
            config.clock = clock_;
            config.rtp.local_ssrc = kDefaultVideoSsrc;
            // 远端视频主SSRC(从SDP的a=ssrc解析): RTCP接收解析SR时过滤用
            config.rtp.remote_ssrc = remote_video_ssrc_;
            video_receive_stream_ = std::make_unique<VideoReceiveStream>(config);
        }
        
        break;
    }
}


int PeerConnection::SendRtp(const char* data, size_t len) {
    if (transport_controller_) {
        // todo: 需要根据实际情况完善
        return transport_controller_->SendRtp("audio", data, len);
    }

    return -1;
}

int PeerConnection::SendRtcp(const char* data, size_t len) {
    if (transport_controller_) {
        // todo: 需要根据实际情况完善
        return transport_controller_->SendRtcp("audio", data, len);
    }

    return -1;
}

} // namespace xrtc


