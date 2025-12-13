#ifndef QUIC_STREAM_STATE_IF_MACHINE_INTERFACE
#define QUIC_STREAM_STATE_IF_MACHINE_INTERFACE

#include <cstdint>

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
    IStreamStateMachine(StreamState state = StreamState::kUnknown):
        state_(state) {}
    virtual ~IStreamStateMachine() {}

    // current process frame type
    // return true if the state machine accept frame type, otherwise return false.
    virtual bool OnFrame(uint16_t frame_type) = 0;

    // check if can send this frame type?
    // return true if the state machine can send this frame type, otherwise return false.
    virtual bool CheckCanSendFrame(uint16_t frame_type) = 0;

    // get current state machine state
    StreamState GetStatus() { return state_; }

protected:
    StreamState state_;
};

}  // namespace quic
}  // namespace quicx

#endif
