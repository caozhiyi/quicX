#include "common/log/log.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/stream/recv_state_machine.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/connection/connection_interface.h"
#include "quic/frame/stream_data_blocked_frame.h"

namespace quicx {

RecvStreamStateMachine::RecvStreamStateMachine(StreamStatus s):
    StreamStateMachine(s) {

}

RecvStreamStateMachine::~RecvStreamStateMachine() {

}

bool RecvStreamStateMachine::OnFrame(uint16_t frame_type) {
    if (frame_type >= FT_STREAM && frame_type <= FT_STREAM_MAX) {
        if (frame_type & SFF_FIN) {
            if (_status == SS_RECV) {
                _status = SS_SIZE_KNOWN;
                return true;
            }
        }

        if (_status == SS_RECV || _status == SS_SIZE_KNOWN) {
            return true;
        }
        

    } else if (frame_type == FT_RESET_STREAM) {
        if (_status == SS_RECV || _status == SS_SIZE_KNOWN || _status == SS_DATA_RECVD) {
            _status = SS_RESET_RECVD;
            return true;
        }

    } else {
        LOG_ERROR("invalid frame type on recv stream. type:%d", frame_type);
        return false;
    }
    
    LOG_ERROR("current status not allow recv this frame. status:%d, frame type:%d", _status, frame_type);
    return false;
}

bool RecvStreamStateMachine::OnEvent(RecvStreamEvent event) {
    if (event == RSE_RECV_ALL_DATA) {
        if (_status == SS_SIZE_KNOWN) {
            _status = SS_DATA_RECVD;
        }
        
    } else if (event == RSE_READ_ALL_DATA) {
        if (_status == SS_DATA_RECVD) {
            _status = SS_DATA_READ;
        }

    } else {
        if (_status == SS_RESET_RECVD) {
            _status = SS_RESET_READ;
        }
    }
    return true;
}

}
