#include "common/log/log.h"
#include "common/util/time.h"

#include "quic/frame/type.h"
#include "quic/common/version.h"
#include "quic/frame/ack_frame.h"
#include "quic/connection/util.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/ping_frame.h"
#include "quic/packet/init_packet.h"
#include "quic/frame/padding_frame.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/quicx/global_resource.h"
#include "quic/packet/handshake_packet.h"
#include "quic/packet/header/long_header.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "quic/connection/controler/send_manager.h"

namespace quicx {
namespace quic {

SendManager::SendManager(std::shared_ptr<common::ITimer> timer):
    timer_(timer),
    send_control_(timer),
    active_send_stream_set_1_is_current_(true) {
    pacing_timer_task_ = common::TimerTask();
    pacing_timer_task_.SetTimeoutCallback([this]() {
        if (send_retry_cb_) {
            send_retry_cb_();
        }
    });
    
    send_control_.SetPacketLostCallback([this](std::shared_ptr<IPacket> packet) {
        common::LOG_WARN("SendManager: packet %llu lost, triggering retransmission", packet->GetPacketNumber());
        if (send_retry_cb_) {
            send_retry_cb_();
        }
    });
}

SendManager::~SendManager() {}

void SendManager::UpdateConfig(const TransportParam& tp) {
    send_control_.UpdateConfig(tp);
}

SendOperation SendManager::GetSendOperation() {
    // Check both sets for active streams
    bool has_active_streams = !active_send_stream_set_1_.empty() || !active_send_stream_set_2_.empty();
    if (wait_frame_list_.empty() && !has_active_streams) {
        return SendOperation::kAllSendDone;

    } else {
        uint64_t can_send_size = 1500;  // TODO: set to mtu size
        uint64_t now = common::UTCTimeMsec();
        send_control_.CanSend(now, can_send_size);
        if (can_send_size == 0) {
            uint64_t next_time = send_control_.GetNextSendTime(now);
            if (next_time > now) {
                uint64_t delay = next_time - now;
                timer_->AddTimer(pacing_timer_task_, delay);
                common::LOG_DEBUG("pacing limited. delay:%llu", delay);
            } else {
                is_cwnd_limited_ = true;
                common::LOG_WARN("congestion control send data limited.");
            }
            return SendOperation::kNextPeriod;
        }
    }
    is_cwnd_limited_ = false;
    return SendOperation::kSendAgainImmediately;
}

void SendManager::ToSendFrame(std::shared_ptr<IFrame> frame) {
    wait_frame_list_.emplace_front(frame);
}

void SendManager::ActiveStream(std::shared_ptr<IStream> stream) {
    common::LOG_DEBUG("active stream. stream id:%d", stream->GetStreamID());
    // Always add to the write set, which is safe even during MakePacket processing
    auto& write_set = GetWriteActiveSendStreamSet();
    write_set.add(stream);
    common::LOG_DEBUG("active stream added to write set. stream id:%d, is_current:%d", stream->GetStreamID(),
        active_send_stream_set_1_is_current_ ? 1 : 2);
}

bool SendManager::GetSendData(
    std::shared_ptr<common::IBuffer> buffer, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    uint64_t can_send_size = mtu_limit_bytes_;  // respect current MTU limit

    // check congestion control
    send_control_.CanSend(common::UTCTimeMsec(), can_send_size);
    if (can_send_size == 0) {
        common::LOG_WARN("congestion control send data limited.");
        return true;
    }

    // firstly, send resend packet
    if (send_control_.NeedReSend()) {
        auto& lost_list = send_control_.GetLostPacket();
        auto packet = lost_list.front();
        lost_list.pop_front();
        // If the lost packet was our PMTU probe, treat as probe failure and do not retransmit as probe
        if (mtu_probe_inflight_ && packet->GetPacketNumber() &&
            packet->GetPacketNumber() == static_cast<uint64_t>(mtu_probe_packet_number_)) {
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
            FixBufferFrameVisitor frame_visitor(probe_size);
            // Add a PING to ensure ack-eliciting
            auto ping = std::make_shared<PingFrame>();
            frame_visitor.HandleFrame(ping);
            // Call MakePacket to include any pending frames (like RETIRE_CONNECTION_ID)
            auto packet = MakePacket(&frame_visitor, encrypto_level, cryptographer);
            if (!packet) {
                common::LOG_WARN("make PMTU probe packet failed.");
                return false;
            }
            // Pad up to target size AFTER MakePacket has added all pending frames
            uint32_t current_size = frame_visitor.GetBuffer()->GetDataLength();
            if (current_size < probe_size) {
                auto padding_frame = std::make_shared<PaddingFrame>();
                padding_frame->SetPaddingLength(static_cast<uint32_t>(probe_size - current_size));
                if (!frame_visitor.HandleFrame(padding_frame)) {
                    common::LOG_WARN("Failed to add padding to PMTU probe, proceeding with size %u", current_size);
                }
            }
            if (!PacketInit(packet, buffer, &frame_visitor)) {
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
                    if (frame->GetType() == FrameType::kPathChallenge || frame->GetType() == FrameType::kPathResponse) {
                        has_path_frame = true;
                        break;
                    }
                }
                if (!has_path_frame) {
                    common::LOG_DEBUG("Anti-amplification budget exhausted, dropping non-path-validation packet");
                    return true;  // Return true to indicate no error, just no data to send
                }
                // Allow PATH_CHALLENGE/PATH_RESPONSE to bypass budget check
                common::LOG_DEBUG("Allowing path validation frame despite budget limit");
            }
            return PacketInit(packet, buffer, &frame_visitor);
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

        FixBufferFrameVisitor frame_visitor(mtu_limit_bytes_ - 50);  // leave headroom
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
                    if (frame->GetType() == FrameType::kPathChallenge || frame->GetType() == FrameType::kPathResponse) {
                        has_path_frame = true;
                        break;
                    }
                }
                if (!has_path_frame) {
                    common::LOG_DEBUG("Anti-amplification budget exhausted, dropping non-path-validation packet");
                    return true;  // Return true to indicate no error, just no data to send
                }
                // Allow PATH_CHALLENGE/PATH_RESPONSE to bypass budget check
                common::LOG_DEBUG("Allowing path validation frame despite budget limit");
            }
        }
        return PacketInit(packet, buffer, &frame_visitor);
    }

    return true;
}

void SendManager::OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    // Pass to send control for RTT/loss/cc updates
    send_control_.OnPacketAck(common::UTCTimeMsec(), ns, frame);

    common::LOG_DEBUG("SendManager::OnPacketAck: is_cwnd_limited_=%d, send_retry_cb_=%p", 
                      is_cwnd_limited_, (void*)&send_retry_cb_);
    
    if (is_cwnd_limited_) {
        is_cwnd_limited_ = false;
        common::LOG_DEBUG("SendManager::OnPacketAck: clearing is_cwnd_limited_, calling send_retry_cb_");
        if (send_retry_cb_) {
            send_retry_cb_();
            common::LOG_DEBUG("SendManager::OnPacketAck: send_retry_cb_ executed");
        } else {
            common::LOG_WARN("SendManager::OnPacketAck: send_retry_cb_ is null!");
        }
    }

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
                    cursor = cursor - it->GetGap() - 1;  // skip the gap including one unacked
                    uint64_t range_high = cursor;
                    uint64_t range_low =
                        (range_high >= it->GetAckRangeLength()) ? (range_high - it->GetAckRangeLength()) : 0;
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
    CcConfigV2 cfg;  // defaults
    // Reconfigure congestion control
    // The current implementation doesn't expose Configure directly; rebuild via factory path in SendControl
    // So we simulate by resetting RTT to initial via UpdateConfig and relying on controller startup behavior
    TransportParam dummy;
    send_control_.UpdateConfig(dummy);
}

void SendManager::ResetMtuForNewPath() {
    // Conservative default until DPLPMTUD re-probes
    mtu_limit_bytes_ = 1200;  // RFC9000 minimum
}

bool SendManager::CheckAndChargeAmpBudget(uint32_t bytes) {
    // allow up to 3x of received bytes on unvalidated path
    if (!streams_allowed_) {
        if (amp_sent_bytes_ + bytes > 3 * amp_recv_bytes_) {
            common::LOG_DEBUG("anti-amplification: budget exceeded. sent:%llu recv:%llu req:%u", amp_sent_bytes_,
                amp_recv_bytes_, bytes);
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
        case FrameType::kNewConnectionId:
        case FrameType::kRetireConnectionId:
            return true;
        default:
            break;
    }
    return false;
}

void SendManager::ResetAmpBudget() {
    // Provide small initial credit so a single PATH_CHALLENGE can be sent
    amp_recv_bytes_ = 400;  // ~ allows up to 1200 bytes under 3x rule
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

std::shared_ptr<IPacket> SendManager::MakePacket(
    IFrameVisitor* visitor, uint8_t encrypto_level, std::shared_ptr<ICryptographer> cryptographer) {
    // Switch active stream sets at the beginning of MakePacket
    // This ensures that any streams added during callbacks go to the write set,
    // while we process streams from the read set.
    SwitchActiveSendStreamSet();

    std::shared_ptr<IPacket> packet;

    for (auto iter = wait_frame_list_.begin(); iter != wait_frame_list_.end();) {
        if (!IsAllowedOnUnvalidated((*iter)->GetType())) {
            ++iter;  // defer disallowed frames until validation succeeds
            continue;
        }
        if (visitor->HandleFrame(*iter)) {
            iter = wait_frame_list_.erase(iter);
        } else {
            common::LOG_ERROR("handle frame failed.");
            return nullptr;
        }
    }

    // Get reference to the read set (current set being processed)
    auto& read_set = GetReadActiveSendStreamSet();

    // Attach stream frames with encryption-level awareness:
    // - Crypto stream (id == 0) may be sent at any level (Initial/Handshake/1-RTT)
    // - Application streams (id != 0) only at 0-RTT or 1-RTT
    bool need_break = false;
    while (true) {
        if (read_set.empty() || need_break) {
            break;
        }

        // Process streams from the read queue
        while (!read_set.queue.empty()) {
            auto stream = read_set.front();
            if (!stream) {
                read_set.pop();
                continue;
            }

            uint64_t sid = stream->GetStreamID();
            // filter by level for non-crypto streams
            if (sid != 0 && !(encrypto_level == kEarlyData || encrypto_level == kApplication)) {
                // cannot send app stream at this level; stop attaching streams for this packet
                need_break = true;
                break;
            }

            common::LOG_DEBUG("try make send stream data. stream id:%d", stream->GetStreamID());
            auto ret = stream->TrySendData(visitor);
            if (ret == IStream::TrySendResult::kSuccess) {
                // Remove from read set after successful send
                read_set.remove(sid);
                read_set.pop();

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
            padding_frame->SetPaddingLength(1200);  // RFC9000: >=1200
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

        common::LOG_DEBUG("send long header packet. packet type:%d, packet size:%d, scid:%llu", encrypto_level,
            visitor->GetBuffer()->GetDataLength(), cid.Hash());
    }

    auto cid = remote_conn_id_manager_->GetCurrentID();
    packet->GetHeader()->SetDestinationConnectionId(cid.GetID(), cid.GetLength());
    packet->SetPayload(visitor->GetBuffer()->GetSharedReadableSpan());
    packet->SetCryptographer(cryptographer);

    common::LOG_DEBUG("send packet. packet type:%d, packet size:%d, dcid:%llu", encrypto_level,
        visitor->GetBuffer()->GetDataLength(), cid.Hash());

    return packet;
}

bool SendManager::PacketInit(std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer) {
    return PacketInit(packet, buffer, nullptr);
}

bool SendManager::PacketInit(
    std::shared_ptr<IPacket>& packet, std::shared_ptr<common::IBuffer> buffer, IFrameVisitor* visitor) {
    // make packet numer
    uint64_t pkt_number = pakcet_number_.NextPakcetNumber(CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel()));
    packet->SetPacketNumber(pkt_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pkt_number));

    common::LOG_DEBUG("PacketInit: encoding packet %llu", pkt_number);
    if (!packet->Encode(buffer)) {
        common::LOG_ERROR("encode packet error. pkt_number=%llu", pkt_number);
        return false;
    }
    common::LOG_DEBUG("PacketInit: encode success. len=%d", buffer->GetDataLength());

    // Get stream data info and frame type bit from visitor for ACK tracking
    std::vector<StreamDataInfo> stream_data;
    if (visitor) {
        stream_data = visitor->GetStreamDataInfo();

        // Set packet's frame_type_bit from visitor (for ACK-eliciting detection)
        auto fix_visitor = dynamic_cast<FixBufferFrameVisitor*>(visitor);
        if (fix_visitor) {
            uint32_t visitor_frame_bit = fix_visitor->GetFrameTypeBit();
            // Use AddFrameTypeBit to OR the bits (don't overwrite existing bits)
            packet->AddFrameTypeBit(static_cast<FrameTypeBit>(visitor_frame_bit));
            common::LOG_DEBUG("PacketInit: packet_number=%llu, stream_data count=%zu, frame_type_bit=%u", pkt_number,
                stream_data.size(), packet->GetFrameTypeBit());
        }
    }

    send_control_.OnPacketSend(common::UTCTimeMsec(), packet, buffer->GetDataLength(), stream_data);
    return true;
}

// Dual-buffer mechanism implementation (similar to Worker::GetReadActiveSendConnectionSet)
ActiveStreamSet& SendManager::GetReadActiveSendStreamSet() {
    return active_send_stream_set_1_is_current_ ? active_send_stream_set_1_ : active_send_stream_set_2_;
}

ActiveStreamSet& SendManager::GetWriteActiveSendStreamSet() {
    // Write set is always the opposite of read set
    return active_send_stream_set_1_is_current_ ? active_send_stream_set_2_ : active_send_stream_set_1_;
}

void SendManager::SwitchActiveSendStreamSet() {
    // This method is called at the beginning of MakePacket to switch the active sets.
    // It merges any remaining streams from the read set into the write set,
    // then switches the read/write sets so that:
    // - The old write set becomes the new read set (to be processed)
    // - The old read set becomes the new write set (for new additions)
    //
    // This ensures that:
    // 1. Streams added during callbacks (in write set) are safe from being removed
    // 2. Any streams that weren't fully processed in the previous read set are preserved
    // 3. The next MakePacket will process streams from the new read set

    auto& read_set = GetReadActiveSendStreamSet();
    auto& write_set = GetWriteActiveSendStreamSet();

    // Move any remaining streams from read set to write set
    while (!read_set.queue.empty()) {
        auto stream = read_set.front();
        if (stream) {
            write_set.add(stream);
        }
        read_set.pop();
    }
    read_set.clear();

    // Switch the current set flag
    active_send_stream_set_1_is_current_ = !active_send_stream_set_1_is_current_;

    common::LOG_DEBUG("SwitchActiveSendStreamSet: switched to set %d", active_send_stream_set_1_is_current_ ? 1 : 2);
}

}  // namespace quic
}  // namespace quicx