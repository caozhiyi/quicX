#ifndef QUIC_STREAM_STATE_MACHINE_INTERFACE
#define QUIC_STREAM_STATE_MACHINE_INTERFACE

#include <cstdint>
#include "quic/stream/type.h"

namespace quicx {

class IStreamStateMachine {
public:
    IStreamStateMachine(StreamState s = SS_UNKNOW): _state(s) {}
    virtual ~IStreamStateMachine() {}

    virtual bool OnFrame(uint16_t frame_type) = 0;

    StreamState GetStatus() { return _state; }

protected:
    StreamState _state;
};

}

#endif
