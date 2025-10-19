#include "common/log/log.h"
#include "quic/frame/type.h"
#include "common/util/time.h"
#include "quic/common/version.h"
#include "quic/frame/ack_frame.h"
#include "quic/connection/util.h"
#include "quic/frame/ping_frame.h"
#include "quic/packet/init_packet.h"
#include "quic/frame/padding_frame.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/header/long_header.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "quic/connection/controler/send_manager.h"

namespace quicx {
namespace quic {

SendManager::SendManager(std::shared_ptr<common::ITimer> timer): 
    send_control_(timer) {

}

SendManager::~SendManager() {

}

void SendManager::UpdateConfig(const TransportParam& tp) {
    send_control_.UpdateConfig(tp);
}

SendOperation SendManager::GetSendOperation() {
    if (wait_frame_list_.empty() && active_send_stream_ids_.empty()) {
        return SendOperation::kAllSendDone;

    } else {
        uint64_t can_send_size = 1500; // TODO: set to mtu size
        send_control_.CanSend(common::UTCTimeMsec(), can_send_size);
        if (can_send_size == 0) {
            common::LOG_WARN("congestion control send data limited.");
            return SendOperation::kNextPeriod;
        }
    }
    return SendOperation::kSendAgainImmediately;
}

void SendManager::ToSendFrame(std::shared_ptr<IFrame> frame) {
    wait_frame_list_.emplace_front(frame);
}

void SendManager::ActiveStream(std::shared_ptr<IStream> stream) {
    common::LOG_DEBUG("active stream. stream id:%d", stream->GetStreamID());
    if (active_send_stream_ids_.find(stream->GetStreamID()) == active_send_stream_ids_.end()) {
        active_send_stream_ids_.insert(stream->GetStreamID());
        active_send_stream_queue_.push(stream);
    }
}

bool SendManager::GetSendData(std::shared_ptr<common::IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    uint64_t can_send_size = mtu_limit_bytes_; // respect current MTU limit
    send_control_.CanSend(common::UTCTimeMsec(), can_send_size);
    if (can_send_size == 0) {
        common::LOG_WARN("congestion control send data limited.");
        return true;
    }

    // firstly, send resend packet
    if (send_control_.NeedReSend()) {
        auto lost_list = send_control_.GetLostPacket();
        auto packet = lost_list.front();
        lost_list.pop_front();
        // If the lost packet was our PMTU probe, treat as probe failure and do not retransmit as probe
        if (mtu_probe_inflight_ && packet->GetPacketNumber() && packet->GetPacketNumber() == static_cast<uint64_t>(mtu_probe_packet_number_)) {
            OnMtuProbeResult(false);
            // fall through to normal send path below instead of retransmitting probe
        } else {
            return PacketInit(packet, buffer);
        }

    } else {
        // secondly, send new packet
        // If PMTU probe is in-flight and not yet sent, craft a probe packet with PING+PADDING only
        if (mtu_probe_inflight_ && mtu_probe_packet_number_ == 0) {
            uint64_t probe_size = mtu_probe_target_bytes_;
            can_send_size = probe_size;
            FixBufferFrameVisitor frame_visitor(static_cast<uint32_t>(probe_size));
            // Add a PING to ensure ack-eliciting
            auto ping = std::make_shared<PingFrame>();
            frame_visitor.HandleFrame(ping);
            // Pad up to target size
            if (frame_visitor.GetBuffer()->GetDataLength() < probe_size) {
                auto padding_frame = std::make_shared<PaddingFrame>();
                padding_frame->SetPaddingLength(static_cast<uint32_t>(probe_size - frame_visitor.GetBuffer()->GetDataLength()));
                frame_visitor.HandleFrame(padding_frame);
            }
            auto packet = MakePacket(&frame_visitor, encrypto_level, cryptographer);
            if (!packet) {
                common::LOG_WARN("make PMTU probe packet failed.");
                return false;
            }
            if (!PacketInit(packet, buffer)) {
                return false;
            }
            // record probe packet number for ack/loss correlation
            mtu_probe_packet_number_ = packet->GetPacketNumber();
            return true;
        }

        if (!streams_allowed_) {
            // Only allow connection-level control/probing frames
            FixBufferFrameVisitor frame_visitor(mtu_limit_bytes_ - 50);
            auto packet = MakePacket(&frame_visitor, encrypto_level, cryptographer);
            if (!packet) {
                // No frames to send, return true (success with no data)
                return true;
            }
            
            // Get packet size before checking budget
            uint32_t packet_size = frame_visitor.GetBuffer()->GetDataLength();
            
            if (packet_size == 0) {
                // Empty packet, return true (success with no data)
                return true;
            }
            
            // anti-amplification: bytes budget (3x)
            // Note: PATH_CHALLENGE/PATH_RESPONSE frames should always be allowed
            // even if budget is exhausted, to ensure path validation can proceed
            if (!CheckAndChargeAmpBudget(packet_size)) {
                // If we have PATH_CHALLENGE or PATH_RESPONSE, allow it anyway
                bool has_path_frame = false;
                for (auto& frame : packet->GetFrames()) {
                    if (frame->GetType() == FrameType::kPathChallenge || 
                        frame->GetType() == FrameType::kPathResponse) {
                        has_path_frame = true;
                        break;
                    }
                }
                if (!has_path_frame) {
                    common::LOG_DEBUG("Anti-amplification budget exhausted, dropping non-path-validation packet");
                    return true; // Return true to indicate no error, just no data to send
                }
                // Allow PATH_CHALLENGE/PATH_RESPONSE to bypass budget check
                common::LOG_DEBUG("Allowing path validation frame despite budget limit");
            }
            return PacketInit(packet, buffer);
        }

        // check flow control
        std::shared_ptr<IFrame> frame;
        if (!flow_control_->CheckLocalSendDataLimit(can_send_size, frame)) {
            common::LOG_WARN("local send data limited.");
            return false;
        }
        if (frame) {
            ToSendFrame(frame);
        }
    
        FixBufferFrameVisitor frame_visitor(mtu_limit_bytes_ - 50); // leave headroom
        frame_visitor.SetStreamDataSizeLimit(can_send_size);
        auto packet = MakePacket(&frame_visitor, encrypto_level, cryptographer);
        if (!packet) {
            common::LOG_WARN("make packet failed.");
            return false;
        }
        if (!streams_allowed_) {
            uint32_t packet_size = frame_visitor.GetBuffer()->GetDataLength();
            if (!CheckAndChargeAmpBudget(packet_size)) {
                // If we have PATH_CHALLENGE or PATH_RESPONSE, allow it anyway
                bool has_path_frame = false;
                for (auto& frame : packet->GetFrames()) {
                    if (frame->GetType() == FrameType::kPathChallenge || 
                        frame->GetType() == FrameType::kPathResponse) {
                        has_path_frame = true;
                        break;
                    }
                }
                if (!has_path_frame) {
                    common::LOG_DEBUG("Anti-amplification budget exhausted, dropping non-path-validation packet");
                    return true; // Return true to indicate no error, just no data to send
                }
                // Allow PATH_CHALLENGE/PATH_RESPONSE to bypass budget check
                common::LOG_DEBUG("Allowing path validation frame despite budget limit");
            }
        }
        return PacketInit(packet, buffer);
    }

    return true;
}

void SendManager::OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    // Pass to send control for RTT/loss/cc updates
    send_control_.OnPacketAck(common::UTCTimeMsec(), ns, frame);

    // PMTU probe success detection: check if ack covers the probe packet number
    if (mtu_probe_inflight_ && mtu_probe_packet_number_ != 0 &&
        (frame->GetType() == FrameType::kAck || frame->GetType() == FrameType::kAckEcn)) {
        auto ack = std::dynamic_pointer_cast<AckFrame>(frame);
        if (ack) {
            uint64_t largest = ack->GetLargestAck();
            uint64_t probe = mtu_probe_packet_number_;
            if (probe <= largest) {
                // Walk ack ranges to see if probe is acked
                uint64_t cursor = largest;
                uint32_t first_range = ack->GetFirstAckRange();
                // First contiguous range [largest-first_range, largest]
                if (probe >= largest - first_range && probe <= largest) {
                    OnMtuProbeResult(true);
                    return;
                }
                // Iterate additional ranges
                auto ranges = ack->GetAckRange();
                for (auto it = ranges.begin(); it != ranges.end(); ++it) {
                    // move cursor backward across gap and range
                    cursor = cursor - it->GetGap() - 1; // skip the gap including one unacked
                    uint64_t range_high = cursor;
                    uint64_t range_low = (range_high >= it->GetAckRangeLength()) ? (range_high - it->GetAckRangeLength()) : 0;
                    if (probe >= range_low && probe <= range_high) {
                        OnMtuProbeResult(true);
                        return;
                    }
                    cursor = range_low - 1;
                }
            }
        }
    }
}

void SendManager::ResetPathSignals() {
    // Recreate congestion controller with default config and reset RTT estimator via UpdateConfig
    CcConfigV2 cfg; // defaults
    // Reconfigure congestion control
    // The current implementation doesn't expose Configure directly; rebuild via factory path in SendControl
    // So we simulate by resetting RTT to initial via UpdateConfig and relying on controller startup behavior
    TransportParam dummy;
    send_control_.UpdateConfig(dummy);
}

void SendManager::ResetMtuForNewPath() {
    // Conservative default until DPLPMTUD re-probes
    mtu_limit_bytes_ = 1200; // RFC9000 minimum
}

bool SendManager::CheckAndChargeAmpBudget(uint32_t bytes) {
    // allow up to 3x of received bytes on unvalidated path
    if (!streams_allowed_) {
        if (amp_sent_bytes_ + bytes > 3 * amp_recv_bytes_) {
            common::LOG_DEBUG("anti-amplification: budget exceeded. sent:%llu recv:%llu req:%u", amp_sent_bytes_, amp_recv_bytes_, bytes);
            return false;
        }
        amp_sent_bytes_ += bytes;
        return true;
    }
    // When streams are allowed, path is validated; disable amp limit
    return true;
}

bool SendManager::IsAllowedOnUnvalidated(uint16_t type) const {
    if (streams_allowed_) {
        return true;
    }
    switch (type) {
        case FrameType::kPathChallenge:
        case FrameType::kPathResponse:
        case FrameType::kAck:
        case FrameType::kAckEcn:
        case FrameType::kPing:
        case FrameType::kPadding:
            return true;
        default:
            break;
    }
    return false;
}

void SendManager::ResetAmpBudget() {
    // Provide small initial credit so a single PATH_CHALLENGE can be sent
    amp_recv_bytes_ = 400; // ~ allows up to 1200 bytes under 3x rule
    amp_sent_bytes_ = 0;
}

void SendManager::OnCandidatePathBytesReceived(uint32_t bytes) {
    if (!streams_allowed_) {
        amp_recv_bytes_ += bytes;
    }
}

void SendManager::StartMtuProbe() {
    // Minimal skeleton: attempt to raise MTU target slightly
    if (mtu_limit_bytes_ < 1450) {
        mtu_probe_target_bytes_ = 1450;
    } else {
        mtu_probe_target_bytes_ = static_cast<uint16_t>(std::min<int>(mtu_limit_bytes_ + 50, 1500));
    }
    mtu_probe_inflight_ = true;
}

void SendManager::OnMtuProbeResult(bool success) {
    if (!mtu_probe_inflight_) return;
    if (success) {
        mtu_limit_bytes_ = mtu_probe_target_bytes_;
    }
    mtu_probe_inflight_ = false;
}

std::shared_ptr<IPacket> SendManager::MakePacket(IFrameVisitor* visitor, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    std::shared_ptr<IPacket> packet;

    for (auto iter = wait_frame_list_.begin(); iter != wait_frame_list_.end();) {
        if (!IsAllowedOnUnvalidated((*iter)->GetType())) {
            ++iter; // defer disallowed frames until validation succeeds
            continue;
        }
        if (visitor->HandleFrame(*iter)) {
            iter = wait_frame_list_.erase(iter);
        } else {
            common::LOG_ERROR("handle frame failed.");
            return nullptr;
        }
    }

    // Attach stream frames with encryption-level awareness:
    // - Crypto stream (id == 0) may be sent at any level (Initial/Handshake/1-RTT)
    // - Application streams (id != 0) only at 0-RTT or 1-RTT
    bool need_break = false;
    while (true) {
        if (active_send_stream_ids_.empty() || need_break) {
            break;
        }
        
        // then sending frames of stream
        while (!active_send_stream_queue_.empty()) {
            auto stream = active_send_stream_queue_.front();
            uint64_t sid = stream->GetStreamID();
            // filter by level for non-crypto streams
            if (sid != 0 && !(encrypto_level == kEarlyData || encrypto_level == kApplication)) {
                // cannot send app stream at this level; stop attaching streams for this packet
                need_break = true;
                break;
            }

            common::LOG_DEBUG("try make send stream data. stream id:%d", stream->GetStreamID());
            auto ret = (stream)->TrySendData(visitor);
            if (ret == IStream::TrySendResult::kSuccess) {
                active_send_stream_queue_.pop();
                active_send_stream_ids_.erase(stream->GetStreamID());
    
            } else if (ret == IStream::TrySendResult::kFailed) {
                common::LOG_ERROR("get stream send data failed.");
                return nullptr;
    
            } else if (ret == IStream::TrySendResult::kBreak) {
                need_break = true;
                break;
            }
        }
    }

    if (visitor->GetBuffer()->GetDataLength() == 0) {
        // If nothing scheduled, but the current encryption level is Initial, we still must send at least
        // an Initial with PADDING to progress handshake when TLS produced data earlier.
        if (encrypto_level == kInitial) {
            auto padding_frame = std::make_shared<PaddingFrame>();
            padding_frame->SetPaddingLength(1200); // RFC9000: >=1200
            visitor->HandleFrame(padding_frame);
        } else {
            common::LOG_INFO("there is no data to send.");
            return nullptr;
        }
    }

    switch (encrypto_level) {
        case kInitial: {
            packet = std::make_shared<InitPacket>();
            // add padding frame
            auto padding_frame = std::make_shared<PaddingFrame>();
            padding_frame->SetPaddingLength(1300 - visitor->GetBuffer()->GetDataLength());
            visitor->HandleFrame(padding_frame);
            break;
        }
        case kEarlyData: {
            packet = std::make_shared<Rtt0Packet>();
            break;
        }
        case kHandshake: {
            packet = std::make_shared<HandshakePacket>();
            break;
        }
        case kApplication: {
            packet = std::make_shared<Rtt1Packet>();
            break;
        }
    }

    auto header = packet->GetHeader();
    if (header->GetHeaderType() == PacketHeaderType::kLongHeader) {
        auto cid = local_conn_id_manager_->GetCurrentID();
        ((LongHeader*)header)->SetSourceConnectionId(cid.GetID(), cid.GetLength());
        ((LongHeader*)header)->SetVersion(kQuicVersions[0]);

        common::LOG_DEBUG("send long header packet. packet type:%d, packet size:%d, scid:%llu",
            encrypto_level, visitor->GetBuffer()->GetDataLength(), cid.Hash());
    }

    auto cid = remote_conn_id_manager_->GetCurrentID();
    packet->GetHeader()->SetDestinationConnectionId(cid.GetID(), cid.GetLength());
    packet->SetPayload(visitor->GetBuffer()->GetReadSpan());
    packet->SetCryptographer(cryptographer);

    common::LOG_DEBUG("send packet. packet type:%d, packet size:%d, dcid:%llu",
        encrypto_level, visitor->GetBuffer()->GetDataLength(), cid.Hash());
    
    return packet;
}

bool SendManager::PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer) {
    // make packet numer
    uint64_t pkt_number = pakcet_number_.NextPakcetNumber(CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel()));
    packet->SetPacketNumber(pkt_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pkt_number));

    if (!packet->Encode(buffer)) {
        common::LOG_ERROR("encode packet error");
        return false;
    }

    send_control_.OnPacketSend(common::UTCTimeMsec(), packet, buffer->GetDataLength());
    return true;
}

}
}