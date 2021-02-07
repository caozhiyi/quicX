#ifndef QUIC_FRAME_MAX_STREAM_DATA_FRAME
#define QUIC_FRAME_MAX_STREAM_DATA_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class MaxStreamDataFrame : public Frame {
public:
    MaxStreamDataFrame() : Frame(FT_MAX_STREAM_DATA) {}
    ~MaxStreamDataFrame() {}

private:
    uint64_t _stream_id;     // indicating the stream ID of the stream.
    uint64_t _maximum_data;  // the maximum amount of data that can be sent on the entire connection.
};

}

#endif