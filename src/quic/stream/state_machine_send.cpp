#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/state_machine_send.h"

namespace quicx {
namespace quic {

StreamStateMachineSend::StreamStateMachineSend(std::function<void()> stream_close_cb, StreamState state):
    IStreamStateMachine(stream_close_cb, state) {

}

StreamStateMachineSend::~StreamStateMachineSend() {

}

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

bool StreamStateMachineSend::CanSendStrameFrame() {
    return state_ == StreamState::kReady ||
           state_ == StreamState::kSend  ||
           state_ == StreamState::kDataSent;
}

bool StreamStateMachineSend::CanSendAppData() {
    return state_ == StreamState::kReady ||
           state_ == StreamState::kSend;
}

bool StreamStateMachineSend::CanSendDataBlockFrame() {
    return state_ == StreamState::kReady ||
           state_ == StreamState::kSend;
}

bool StreamStateMachineSend::CanSendResetStreamFrame() {
    return state_ == StreamState::kReady ||
           state_ == StreamState::kSend  ||
           state_ == StreamState::kDataSent;
}

}
}