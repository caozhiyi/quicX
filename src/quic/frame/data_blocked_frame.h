#ifndef QUIC_FRAME_DATA_BLOCKED_FRAME
#define QUIC_FRAME_DATA_BLOCKED_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class DataBlockedFrame : public Frame {
public:
    DataBlockedFrame() : Frame(FT_DATA_BLOCKED) {}
    ~DataBlockedFrame() {}

private:
   uint64_t _data_limit;  //  the connection-level limit at which blocking occurred.
};

}

#endif