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
    case SS_RECV:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            if (frame_type & StreamFrameFlag::kFinFlag) {
                state_ = SS_SIZE_KNOWN;
            }
            return true;
        }
        if (frame_type == FrameType::kStreamDataBlocked) {
            return true;
        }
        if (frame_type == FrameType::kResetStream) {
            state_ = SS_RESET_RECVD;
            return true;
        }
        break;

    case SS_SIZE_KNOWN:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            return true;
        }
        if (frame_type == FrameType::kStreamDataBlocked) {
            return true;
        }
        if (frame_type == FrameType::kResetStream) {
            state_ = SS_RESET_RECVD;
            return true;
        }
        break;
        
    case SS_RESET_RECVD:
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
    return state_ == SS_RECV;
}

bool StreamStateMachineRecv::CanSendStopSendingFrame() {
    return state_ != SS_RESET_READ &&
           state_ != SS_RESET_RECVD;
}

bool StreamStateMachineRecv::CanAppReadAllData() {
    return state_ == SS_DATA_RECVD;
}

bool StreamStateMachineRecv::RecvAllData() {
    switch (state_) {
    case SS_SIZE_KNOWN:
        state_ = SS_DATA_RECVD;
        return true;
    case SS_RESET_RECVD:
        state_ = SS_RESET_RECVD;
        return true;
    default:
        common::LOG_ERROR("current status not allow recv all data. status:%d", state_);
        break;
    }
    return false;
}

bool StreamStateMachineRecv::AppReadAllData() {
    switch (state_) {
    case SS_DATA_RECVD:
        state_ = SS_DATA_READ;
        break;
    case SS_RESET_RECVD:
        state_ = SS_RESET_READ;
        break;
    default:
        common::LOG_ERROR("current status not allow read all data. status:%d", state_);
        return false;
    }
    if (state_ == SS_DATA_READ || state_ == SS_RESET_READ) {
        if (stream_close_cb_) {
            stream_close_cb_();
        }
    }
    return true;
}

}
}