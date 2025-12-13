#include "common/log/log.h"

#include "quic/frame/stream_frame.h"
#include "quic/frame/type.h"
#include "quic/stream/state_machine_recv.h"

namespace quicx {
namespace quic {

StreamStateMachineRecv::StreamStateMachineRecv(StreamState state):
    IStreamStateMachine(state),
    is_reset_received_(false) {}

StreamStateMachineRecv::~StreamStateMachineRecv() {}

bool StreamStateMachineRecv::OnFrame(uint16_t frame_type) {
    if (frame_type == FrameType::kResetStream) {
        is_reset_received_ = true;
    }
    switch (state_) {
        case StreamState::kRecv:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                if (frame_type & StreamFrameFlag::kFinFlag) {
                    state_ = StreamState::kSizeKnown;
                }
                return true;
            }
            if (frame_type == FrameType::kStreamDataBlocked) {
                return true;
            }
            if (frame_type == FrameType::kResetStream) {
                state_ = StreamState::kResetRecvd;
                return true;
            }
            break;

        case StreamState::kSizeKnown:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                return true;
            }
            if (frame_type == FrameType::kStreamDataBlocked) {
                return true;
            }
            if (frame_type == FrameType::kResetStream) {
                state_ = StreamState::kResetRecvd;
                return true;
            }
            break;

        case StreamState::kResetRecvd:
            if (StreamFrame::IsStreamFrame(frame_type)) {
                return true;
            }
            break;

        case StreamState::kDataRecvd:
        case StreamState::kDataRead:
            // RFC 9000 Section 4.5: Even in terminal states, we must accept
            // RESET_STREAM and STREAM frames to validate final_size consistency
            if (StreamFrame::IsStreamFrame(frame_type) || frame_type == FrameType::kResetStream) {
                return true;
            }
            break;

        default:
            common::LOG_ERROR("current status not allow recv this frame. status:%d, frame type:%d", state_, frame_type);
            break;
    }
    return false;
}

bool StreamStateMachineRecv::CheckCanSendFrame(uint16_t frame_type) {
    // RFC 9000 Section 3.2: The receiver sends MAX_STREAM_DATA frames in the "Recv" and "Size Known" states
    if (frame_type == FrameType::kMaxStreamData) {
        return state_ == StreamState::kRecv || state_ == StreamState::kSizeKnown;
    }
    // RFC 9000 Section 3.2: A receiver MAY send a STOP_SENDING frame in any state where it has not
    // received a RESET_STREAM frame -- that is, states other than "Reset Recvd" or "Reset Read"
    if (frame_type == FrameType::kStopSending) {
        return state_ != StreamState::kResetRead && state_ != StreamState::kResetRecvd;
    }
    return false;
}

bool StreamStateMachineRecv::CanAppReadAllData() {
    return state_ == StreamState::kDataRecvd;
}

bool StreamStateMachineRecv::RecvAllData() {
    switch (state_) {
        case StreamState::kSizeKnown:
            if (is_reset_received_) {
                state_ = StreamState::kResetRecvd;
            } else {
                state_ = StreamState::kDataRecvd;
            }
            return true;
        case StreamState::kResetRecvd:
            // RFC 9000 Section 3.2: Optional transition from Reset Recvd to Data Recvd
            // when all data has been received (for final_size validation)
            // State remains kResetRecvd, ready for AppReadAllData()
            return true;
        default:
            common::LOG_ERROR("current status not allow recv all data. status:%d", state_);
            break;
    }
    return false;
}

bool StreamStateMachineRecv::AppReadAllData() {
    switch (state_) {
        case StreamState::kDataRecvd:
            state_ = StreamState::kDataRead;
            break;
        case StreamState::kResetRecvd:
            state_ = StreamState::kResetRead;
            break;
        default:
            common::LOG_ERROR("current status not allow read all data. status:%d", state_);
            return false;
    }
    return true;
}

}  // namespace quic
}  // namespace quicx