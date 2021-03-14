#ifndef QUIC_STREAM_STREAM_STATE_MACHINE_INTERFACE
#define QUIC_STREAM_STREAM_STATE_MACHINE_INTERFACE

#include <cstdint>
#include "type.h"

namespace quicx {

class StreamStateMachine {
public:
    StreamStateMachine() {}
    virtual ~StreamStateMachine() {}

    virtual bool OnFrame(uint16_t frame_type) = 0;

    StreamStatus GetStatus() { return _status; }

protected:
    StreamStatus _status;
};

}

#endif
