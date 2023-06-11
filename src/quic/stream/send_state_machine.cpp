#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/send_state_machine.h"

namespace quicx {

SendStreamStateMachine::SendStreamStateMachine(StreamState s):
    IStreamStateMachine(s) {

}

SendStreamStateMachine::~SendStreamStateMachine() {

}

bool SendStreamStateMachine::OnFrame(uint16_t frame_type) {
    switch (_state) {
    case SS_READY:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            _state = SS_SEND;
            if (frame_type & SFF_FIN) {
                _state = SS_DATA_SENT;
            }
            return true;
        }
        if (frame_type == FT_STREAM_DATA_BLOCKED) {
            _state = SS_SEND;
            return true;
        }
        if (frame_type == FT_RESET_STREAM) {
            _state = SS_RESET_SENT;
            return true;
        }
        break;
    case SS_SEND:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            if (frame_type & SFF_FIN) {
                _state = SS_DATA_SENT;
            }
            return true;
        }
        if (frame_type == FT_RESET_STREAM) {
            _state = SS_RESET_SENT;
            return true;
        }
        break;
    case SS_DATA_SENT:
        if (frame_type == FT_RESET_STREAM) {
            _state = SS_RESET_SENT;
            return true;
        }
        break;
    }
    LOG_ERROR("current status not allow send this frame. status:%d, frame type:%d", _state, frame_type);
    return false;
}

bool SendStreamStateMachine::AllAckDone() {
    switch (_state) {
    case SS_DATA_SENT:
        _state = SS_DATA_RECVD;
        break;
    case SS_RESET_SENT:
        _state = SS_RESET_RECVD;
        break;
    default:
        LOG_ERROR("current status not allow ack done. status:%d", _state);
        return false;
    }
    if (_state == SS_DATA_RECVD || _state == SS_RESET_RECVD) {
        if (_stream_close_cb) {
            _stream_close_cb();
        }
    }
    return true;
}

bool SendStreamStateMachine::CanSendStrameFrame() {
    return _state == SS_READY ||
           _state == SS_SEND  ||
           _state == SS_DATA_SENT;
}

bool SendStreamStateMachine::CanSendAppData() {
    return _state == SS_READY ||
           _state == SS_SEND;
}

bool SendStreamStateMachine::CanSendDataBlockFrame() {
    return _state == SS_READY ||
           _state == SS_SEND;
}

bool SendStreamStateMachine::CanSendResetStreamFrame() {
    return _state == SS_READY ||
           _state == SS_SEND  ||
           _state == SS_DATA_SENT;
}

}
