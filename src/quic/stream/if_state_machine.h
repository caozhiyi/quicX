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
    // state_change_cb: callback(stream_id, old_state, new_state)
    using StateChangeCB = std::function<void(uint64_t, StreamState, StreamState)>;

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

    // set state change callback for qlog
    void SetStateChangeCB(StateChangeCB cb, uint64_t stream_id) {
        state_change_cb_ = cb;
        stream_id_ = stream_id;
    }

protected:
    // Notify state change (call after state_ is updated)
    void NotifyStateChange(StreamState old_state, StreamState new_state) {
        if (state_change_cb_ && old_state != new_state) {
            state_change_cb_(stream_id_, old_state, new_state);
        }
    }

protected:
    StreamState state_;
    StateChangeCB state_change_cb_;
    uint64_t stream_id_ = 0;
};

}  // namespace quic
}  // namespace quicx

#endif
