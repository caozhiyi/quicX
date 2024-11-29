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
    case SS_READY:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            state_ = SS_SEND;
            if (frame_type & SFF_FIN) {
                state_ = SS_DATA_SENT;
            }
            return true;
        }
        if (frame_type == FT_STREAM_DATA_BLOCKED) {
            state_ = SS_SEND;
            return true;
        }
        if (frame_type == FT_RESET_STREAM) {
            state_ = SS_RESET_SENT;
            return true;
        }
        break;
    case SS_SEND:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            if (frame_type & SFF_FIN) {
                state_ = SS_DATA_SENT;
            }
            return true;
        }
        if (frame_type == FT_RESET_STREAM) {
            state_ = SS_RESET_SENT;
            return true;
        }
        break;
    case SS_DATA_SENT:
        if (frame_type == FT_RESET_STREAM) {
            state_ = SS_RESET_SENT;
            return true;
        }
        break;
    }
    common::LOG_ERROR("current status not allow send this frame. status:%d, frame type:%d", state_, frame_type);
    return false;
}

bool StreamStateMachineSend::AllAckDone() {
    switch (state_) {
    case SS_DATA_SENT:
        state_ = SS_DATA_RECVD;
        break;
    case SS_RESET_SENT:
        state_ = SS_RESET_RECVD;
        break;
    default:
        common::LOG_ERROR("current status not allow ack done. status:%d", state_);
        return false;
    }
    if (state_ == SS_DATA_RECVD || state_ == SS_RESET_RECVD) {
        if (stream_close_cb_) {
            stream_close_cb_();
        }
    }
    return true;
}

bool StreamStateMachineSend::CanSendStrameFrame() {
    return state_ == SS_READY ||
           state_ == SS_SEND  ||
           state_ == SS_DATA_SENT;
}

bool StreamStateMachineSend::CanSendAppData() {
    return state_ == SS_READY ||
           state_ == SS_SEND;
}

bool StreamStateMachineSend::CanSendDataBlockFrame() {
    return state_ == SS_READY ||
           state_ == SS_SEND;
}

bool StreamStateMachineSend::CanSendResetStreamFrame() {
    return state_ == SS_READY ||
           state_ == SS_SEND  ||
           state_ == SS_DATA_SENT;
}

}
}