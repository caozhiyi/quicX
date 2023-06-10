#include "common/log/log.h"
#include "quic/frame/type.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/recv_state_machine.h"

namespace quicx {

RecvStreamStateMachine::RecvStreamStateMachine(StreamState s):
    IStreamStateMachine(s) {

}

RecvStreamStateMachine::~RecvStreamStateMachine() {

}

bool RecvStreamStateMachine::OnFrame(uint16_t frame_type) {
    switch (_state) {
    case SS_RECV:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            if (frame_type & SFF_FIN) {
                _state = SS_SIZE_KNOWN;
            }
            return true;
        }
        if (frame_type == FT_STREAM_DATA_BLOCKED) {
            return true;
        }
        if (frame_type == FT_RESET_STREAM) {
            _state = SS_RESET_RECVD;
            return true;
        }
        break;
    case SS_SIZE_KNOWN:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            return true;
        }
        if (frame_type == FT_STREAM_DATA_BLOCKED) {
            return true;
        }
        if (frame_type == FT_RESET_STREAM) {
            _state = SS_RESET_RECVD;
            return true;
        }
        break;
    case SS_RESET_RECVD:
        if (StreamFrame::IsStreamFrame(frame_type)) {
            return true;
        }
        break;
    }

    LOG_ERROR("current status not allow recv this frame. status:%d, frame type:%d", _state, frame_type);
    return false;
}

bool RecvStreamStateMachine::RecvAllData() {
    switch (_state) {
    case SS_SIZE_KNOWN:
        _state = SS_DATA_RECVD;
        return true;
    case SS_RESET_RECVD:
        _state = SS_RESET_RECVD;
        return true;
    }
    LOG_ERROR("current status not allow recv all data. status:%d", _state);
    return false;
}

bool RecvStreamStateMachine::AppReadAllData() {
    switch (_state) {
    case SS_DATA_RECVD:
        _state = SS_DATA_READ;
        return true;
    case SS_RESET_RECVD:
        _state = SS_RESET_READ;
        return true;
    }
    LOG_ERROR("current status not allow read all data. status:%d", _state);
    return false;
}

}
