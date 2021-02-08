#ifndef QUIC_FRAME_RETIRE_CONNECTION_ID_FRAME
#define QUIC_FRAME_RETIRE_CONNECTION_ID_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class RetireConnectionIDFrame : public Frame {
public:
    RetireConnectionIDFrame() : Frame(FT_RETIRE_CONNECTION_ID) {}
    ~RetireConnectionIDFrame() {}

private:
    uint64_t _sequence_number;  // the connection ID being retired.
};

}

#endif