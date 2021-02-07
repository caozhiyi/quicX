#ifndef QUIC_FRAME_MAX_STREAMS_FRAME
#define QUIC_FRAME_MAX_STREAMS_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class MaxStreamsFrame : public Frame {
public:
    MaxStreamsFrame() : Frame(FT_MAX_STREAMS) {}
    ~MaxStreamsFrame() {}

private:
   uint64_t _maximum_streams;  // A count of the cumulative number of streams of the corresponding type that can be opened over the lifetime of the connection.
};

}

#endif