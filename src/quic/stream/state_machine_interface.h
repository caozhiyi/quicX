#ifndef QUIC_STREAM_STATE_MACHINE_INTERFACE
#define QUIC_STREAM_STATE_MACHINE_INTERFACE

#include <cstdint>
#include "quic/stream/type.h"

namespace quicx {

class IStreamStateMachine {
public:
    IStreamStateMachine(StreamState s = SS_UNKNOW): _state(s) {}
    virtual ~IStreamStateMachine() {}

    // current recv frame type
    // return false if the state machine refuse frame type  
    virtual bool OnFrame(uint16_t frame_type) = 0;

    // get current state machine state
    StreamState GetStatus() { return _state; }

    typedef std::function<void()> StreamCloseCB;
    void SetStreamCloseCB(StreamCloseCB cb) { _stream_close_cb = cb; }

protected:
    StreamState _state;
    StreamCloseCB _stream_close_cb;
};

}

#endif
