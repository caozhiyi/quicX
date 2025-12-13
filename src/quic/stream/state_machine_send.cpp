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
    switch (state_) {
        case StreamState::kReady:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                state_ = StreamState::kSend;
                if (frame_type & StreamFrameFlag::kFinFlag) {
                    state_ = StreamState::kDataSent;
                }
                return true;
            }
            if (frame_type == FrameType::kStreamDataBlocked) {
                state_ = StreamState::kSend;
                return true;
            }
            if (frame_type == FrameType::kResetStream) {
                state_ = StreamState::kResetSent;
                return true;
            }
            break;
        case StreamState::kSend:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                if (frame_type & StreamFrameFlag::kFinFlag) {
                    state_ = StreamState::kDataSent;
                }
                return true;
            }
            if (frame_type == FrameType::kResetStream) {
                state_ = StreamState::kResetSent;
                return true;
            }
            break;
        case StreamState::kDataSent:
            if (frame_type == FrameType::kResetStream) {
                state_ = StreamState::kResetSent;
                return true;
            }
            break;
        default:
            common::LOG_ERROR("current status not allow send this frame. status:%d, frame type:%d", state_, frame_type);
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
    switch (state_) {
        case StreamState::kDataSent:
            state_ = StreamState::kDataRecvd;
            break;
        case StreamState::kResetSent:
            state_ = StreamState::kResetRecvd;
            break;
        default:
            common::LOG_ERROR("current status not allow ack done. status:%d", state_);
            return false;
    }
    return true;
}

}  // namespace quic
}  // namespace quicx