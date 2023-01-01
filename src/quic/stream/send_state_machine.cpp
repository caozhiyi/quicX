#include "common/log/log.h"

#include "quic/frame/stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/send_state_machine.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/connection/connection_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {

SendStreamStateMachine::SendStreamStateMachine(StreamState s):
    IStreamStateMachine(s) {

}

SendStreamStateMachine::~SendStreamStateMachine() {

}

bool SendStreamStateMachine::OnFrame(uint16_t frame_type) {
    if (frame_type >= FT_STREAM && frame_type <= FT_STREAM_MAX) {
        if (_state == SS_READY) {
            _state = SS_SEND;
            if (frame_type & SFF_FIN) {
                _state = SS_DATA_SENT;
            }
            return true;
        }
        if (frame_type & SFF_FIN) {
            if (_state == SS_SEND) {
                _state = SS_DATA_SENT;
                return true;
            }
        }

        if (_state == SS_READY || _state == SS_SEND) {
            return true;
        }

    } else if (frame_type == FT_STREAM_DATA_BLOCKED) {
        if (_state == SS_READY) {
            _state = SS_SEND;
            return true;
        }

        if (_state == SS_READY || _state == SS_SEND) {
            return true;
        }

    }  else if (frame_type == FT_RESET_STREAM) {
        if (_state == SS_READY || _state == SS_SEND || _state == SS_DATA_SENT) {
            _state = SS_RESET_SENT;
            return true;
        }

    } else {
        LOG_ERROR("invalid frame type on send stream. type:%d", frame_type);
        return false;
    }

    LOG_ERROR("current status not allow send this frame. status:%d, frame type:%d", _state, frame_type);
    return false;
}

bool SendStreamStateMachine::RecvAllAck() {
    if (_state == SS_DATA_SENT) {
        _state = SS_DATA_RECVD;
        return true;
    }

    if (_state == SS_RESET_SENT) {
        _state = SS_RESET_RECVD;
        return true;
    }

    LOG_ERROR("current status not allow recv this ack. status:%d", _state);
    return false;
}

}
