#include "common/log/log.h"

#include "quic/frame/stream_frame.h"
#include "quic/frame/type.h"
#include "quic/stream/state_machine_send.h"
#include "quic/stream/type.h"

namespace quicx {
namespace quic {

StreamStateMachineSend::StreamStateMachineSend(StreamState state):
    IStreamStateMachine(state) {}

StreamStateMachineSend::~StreamStateMachineSend() {}

bool StreamStateMachineSend::OnFrame(uint16_t frame_type) {
    StreamState old_state = state_;
    switch (state_) {
        case StreamState::kReady:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                state_ = StreamState::kSend;
                if (frame_type & StreamFrameFlag::kFinFlag) {
                    state_ = StreamState::kDataSent;
                }
                NotifyStateChange(old_state, state_);
                return true;
            }
            if (frame_type == FrameType::kStreamDataBlocked) {
                state_ = StreamState::kSend;
                NotifyStateChange(old_state, state_);
                return true;
            }
            if (frame_type == FrameType::kResetStream) {
                state_ = StreamState::kResetSent;
                NotifyStateChange(old_state, state_);
                return true;
            }
            break;
        case StreamState::kSend:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                if (frame_type & StreamFrameFlag::kFinFlag) {
                    state_ = StreamState::kDataSent;
                    NotifyStateChange(old_state, state_);
                }
                return true;
            }
            if (frame_type == FrameType::kResetStream) {
                state_ = StreamState::kResetSent;
                NotifyStateChange(old_state, state_);
                return true;
            }
            break;
        case StreamState::kDataSent:
            if (frame_type == FrameType::kResetStream) {
                state_ = StreamState::kResetSent;
                NotifyStateChange(old_state, state_);
                return true;
            }
            break;
        default:
            LOG_ERROR("current status not allow send this frame. status:%d, frame type:%d", state_, frame_type);
            break;
    }
    return false;
}

bool StreamStateMachineSend::CheckCanSendFrame(uint16_t frame_type) {
    // RFC 9000 Section 3.3: A sender MUST NOT send any of these frames from a terminal state
    if (state_ == StreamState::kResetRecvd || state_ == StreamState::kDataRecvd) {
        return false;
    }

    // RFC 9000 Section 3.1: A sender MUST NOT send a STREAM or STREAM_DATA_BLOCKED frame
    // for a stream in the "Reset Sent" state or the "Data Sent" state
    if (StreamFrame::IsStreamFrame(frame_type) || frame_type == FrameType::kStreamDataBlocked) {
        return state_ != StreamState::kResetSent && state_ != StreamState::kDataSent;
    }

    return true;
}

bool StreamStateMachineSend::AllAckDone() {
    StreamState old_state = state_;
    switch (state_) {
        case StreamState::kDataSent:
            state_ = StreamState::kDataRecvd;
            NotifyStateChange(old_state, state_);
            break;
        case StreamState::kResetSent:
            state_ = StreamState::kResetRecvd;
            NotifyStateChange(old_state, state_);
            break;
        case StreamState::kDataRecvd:
        case StreamState::kResetRecvd:
            // Idempotent re-entry, not an error.
            //
            // SendStream::CheckAllDataAcked() re-fires on every ACK that leaves
            // `fin_sent_ && acked_offset_ >= send_data_offset_` satisfied, and
            // QUIC routinely delivers more than one such ACK for a single
            // stream: when the same bytes were carried by both the original
            // packet and a retransmission, the peer ACKs both packet numbers
            // and OnDataAcked() runs twice for one logical range.
            //
            // The terminal state is already correct here, so "all acks done" is
            // trivially true. Returning false would tell the caller the
            // transition failed when nothing did, and logging at ERROR level
            // drowned out real failures during the handshakeloss investigation.
            LOG_DEBUG("AllAckDone on already-terminal state %d, ignoring", static_cast<int>(state_));
            break;
        default:
            LOG_ERROR("current status not allow ack done. status:%d", static_cast<int>(state_));
            return false;
    }
    return true;
}

}  // namespace quic
}  // namespace quicx