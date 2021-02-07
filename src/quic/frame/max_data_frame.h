#ifndef QUIC_FRAME_MAX_DATA_FRAME
#define QUIC_FRAME_MAX_DATA_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class MaxDataFrame : public Frame {
public:
    MaxDataFrame() : Frame(FT_MAX_DATA) {}
    ~MaxDataFrame() {}

private:
    uint64_t _maximum_data;  // the maximum amount of data that can be sent on the entire connection.
};

}

#endif