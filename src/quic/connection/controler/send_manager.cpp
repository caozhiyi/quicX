#include "common/log/log.h"
#include "common/util/time.h"

#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/type.h"

namespace quicx {
namespace quic {

SendManager::SendManager(std::shared_ptr<common::ITimer> timer):
    send_control_(timer),
    send_flow_controller_(nullptr),
    pakcet_number_() {
    pacing_timer_task_ = common::TimerTask();
    pacing_timer_task_.SetTimeoutCallback([this]() {
        if (send_retry_cb_) {
            send_retry_cb_();
        }
    });

    send_control_.SetPacketLostCallback([this](std::shared_ptr<IPacket> packet) {
        common::LOG_WARN("SendManager: packet %llu lost, triggering retransmission", packet->GetPacketNumber());
        // Note: send_retry_cb_ (which calls BaseConnection::ActiveSend) will check connection state
        // and ignore the callback if connection is closing/draining/closed
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
    // Check if there are frames or active streams to send
    bool has_active_data = !wait_frame_list_.empty();
    if (stream_manager_) {
        has_active_data = has_active_data || stream_manager_->HasActiveStreams();
    }

    if (!has_active_data) {
        return SendOperation::kAllSendDone;

    } else {
        uint64_t can_send_size = 1500;  // TODO: set to mtu size
        uint64_t now = common::UTCTimeMsec();
        send_control_.CanSend(now, can_send_size);
        if (can_send_size == 0) {
            // RFC 9002: Allow ACK-only packets to bypass congestion control
            if (!IsCongestionControlExempt()) {
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
            // Have ACK-only frames, allow sending
            common::LOG_DEBUG("GetSendOperation: bypassing congestion for ACK-only frames");
        }
    }
    is_cwnd_limited_ = false;
    return SendOperation::kSendAgainImmediately;
}

void SendManager::ToSendFrame(std::shared_ptr<IFrame> frame) {
    wait_frame_list_.emplace_front(frame);
}

void SendManager::OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    // Pass to send control for RTT/loss/cc updates
    send_control_.OnPacketAck(common::UTCTimeMsec(), ns, frame);

    common::LOG_DEBUG(
        "SendManager::OnPacketAck: is_cwnd_limited_=%d, send_retry_cb_=%p", is_cwnd_limited_, (void*)&send_retry_cb_);

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
    mtu_probe_inflight_ = false;
    mtu_probe_packet_number_ = 0;
    // Conservative start
    mtu_limit_bytes_ = 1200;  // RFC 9000 min
}

void SendManager::ClearActiveStreams() {
    if (stream_manager_) {
        stream_manager_->ClearActiveStreams();
    }
    wait_frame_list_.clear();
    send_control_.ClearRetransmissionData();
}

void SendManager::ClearRetransmissionData() {
    send_control_.ClearRetransmissionData();
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
        case FrameType::kConnectionClose:
        case FrameType::kConnectionCloseApp:
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

bool SendManager::ShouldSendRetry() const {
    // Only consider Retry if path is unvalidated
    if (streams_allowed_) {
        return false;
    }

    // No bytes received yet, can't send Retry
    if (amp_recv_bytes_ == 0) {
        return false;
    }

    // RFC 9000: Trigger Retry when approaching 3x limit
    // Send Retry at 2x to have room for the Retry packet itself
    return amp_sent_bytes_ >= 2 * amp_recv_bytes_;
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

// RFC 9002: Check if frames are exempt from congestion control (ACKs, CONNECTION_CLOSE)
bool SendManager::IsCongestionControlExempt() const {
    // Don't check active streams - even if streams are waiting, we should send ACKs/Close first
    // Only check the wait_frame_list_ for what's immediately pending

    if (wait_frame_list_.empty()) {
        common::LOG_DEBUG("IsCongestionControlExempt: no pending frames, returning false");
        return false;
    }

    // Check if all pending frames are exempt (ACKs or CONNECTION_CLOSE)
    size_t total_frames = wait_frame_list_.size();
    size_t exempt_frames = 0;
    for (const auto& frame : wait_frame_list_) {
        auto frame_type = frame->GetType();
        if (frame_type == FrameType::kAck || frame_type == FrameType::kAckEcn ||
            frame_type == FrameType::kConnectionClose || frame_type == FrameType::kConnectionCloseApp) {
            exempt_frames++;
        } else {
            common::LOG_DEBUG("IsCongestionControlExempt: found non-exempt frame type=%d, returning false", frame_type);
            return false;
        }
    }

    common::LOG_DEBUG("IsCongestionControlExempt: all %zu pending frames are exempt, returning true", exempt_frames);
    return true;
}

void SendManager::SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) {
    qlog_trace_ = trace;
    send_control_.SetQlogTrace(trace);
}

uint32_t SendManager::GetAvailableWindow() {
    uint64_t can_send_size = mtu_limit_bytes_;
    uint64_t now = common::UTCTimeMsec();
    send_control_.CanSend(now, can_send_size);

    common::LOG_DEBUG("SendManager::GetAvailableWindow: can_send=%llu bytes", can_send_size);
    return static_cast<uint32_t>(can_send_size);
}

std::vector<std::shared_ptr<IFrame>> SendManager::GetPendingFrames(EncryptionLevel level, uint32_t max_bytes) {
    std::vector<std::shared_ptr<IFrame>> result;
    uint32_t total_bytes = 0;

    // Iterate through wait_frame_list_ and collect frames suitable for this encryption level
    for (auto iter = wait_frame_list_.begin(); iter != wait_frame_list_.end();) {
        auto frame = *iter;
        uint32_t frame_size = frame->EncodeSize();

        // Check if adding this frame would exceed max_bytes
        if (total_bytes + frame_size > max_bytes) {
            common::LOG_DEBUG("SendManager::GetPendingFrames: reached max_bytes limit (%u), stopping", max_bytes);
            break;
        }

        // Check if frame is suitable for this encryption level
        // TODO: Add proper encryption level filtering if needed
        // For now, assume all frames in wait_frame_list_ are suitable

        result.push_back(frame);
        total_bytes += frame_size;
        iter = wait_frame_list_.erase(iter);  // Remove from pending list
    }

    common::LOG_DEBUG("SendManager::GetPendingFrames: level=%d, collected %zu frames, total=%u bytes", level,
        result.size(), total_bytes);
    return result;
}

bool SendManager::HasStreamData(EncryptionLevel level) {
    if (!stream_manager_) {
        return false;
    }

    // Check if StreamManager has active streams
    bool has_data = stream_manager_->HasActiveStreams();

    common::LOG_DEBUG("SendManager::HasStreamData: level=%d, has_data=%d", level, has_data);
    return has_data;
}

}  // namespace quic
}  // namespace quicx