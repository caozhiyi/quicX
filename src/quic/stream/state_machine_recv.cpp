#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/state_machine_recv.h"

namespace quicx {
namespace quic {

StreamStateMachineRecv::StreamStateMachineRecv(std::function<void()> stream_close_cb, StreamState state):
    IStreamStateMachine(stream_close_cb, state) {

}

StreamStateMachineRecv::~StreamStateMachineRecv() {

}

bool StreamStateMachineRecv::OnFrame(uint16_t frame_type) {
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
    default:
        common::LOG_ERROR("current status not allow recv this frame. status:%d, frame type:%d", state_, frame_type);
        break;
    }
    return false;
}

bool StreamStateMachineRecv::CanSendMaxStrameDataFrame() {
    return state_ == StreamState::kRecv;
}

bool StreamStateMachineRecv::CanSendStopSendingFrame() {
    return state_ != StreamState::kResetRead &&
           state_ != StreamState::kResetRecvd;
}

bool StreamStateMachineRecv::CanAppReadAllData() {
    return state_ == StreamState::kDataRecvd;
}

bool StreamStateMachineRecv::RecvAllData() {
    switch (state_) {
    case StreamState::kSizeKnown:
        state_ = StreamState::kDataRecvd;
        return true;
    case StreamState::kResetRecvd:
        state_ = StreamState::kResetRecvd;
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
    if (state_ == StreamState::kDataRead || state_ == StreamState::kResetRead) {
        if (stream_close_cb_) {
            stream_close_cb_();
        }
    }
    return true;
}

}
}