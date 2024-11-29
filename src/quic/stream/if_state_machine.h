#ifndef QUIC_STREAM_STATE_IF_MACHINE_INTERFACE
#define QUIC_STREAM_STATE_IF_MACHINE_INTERFACE

#include <cstdint>
#include <functional>
#include "quic/stream/type.h"

namespace quicx {
namespace quic {

/*
 stream state machine interface
 send and recv stream state machine implement this interface
*/
class IStreamStateMachine {
public:
    // stream_close_cb: called when stream is going to close
    // state: initial state
    IStreamStateMachine(std::function<void()> stream_close_cb, StreamState state = SS_UNKNOW):
        stream_close_cb_(stream_close_cb), state_(state) {}
    virtual ~IStreamStateMachine() {}

    // current process frame type
    // return false if the state machine refuse frame type  
    virtual bool OnFrame(uint16_t frame_type) = 0;

    // get current state machine state
    StreamState GetStatus() { return state_; }

protected:
    StreamState state_;
    std::function<void()> stream_close_cb_;
};

}
}

#endif
