#ifndef QUIC_FRAME_STREAM_DATA_BLOCKED_FRAME
#define QUIC_FRAME_STREAM_DATA_BLOCKED_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StreamDataBlockedFrame : public Frame {
public:
    StreamDataBlockedFrame() : Frame(FT_STREAM_DATA_BLOCKED) {}
    ~StreamDataBlockedFrame() {}

private:
   uint64_t _stream_id;   // the stream which is flow control blocked.
   uint64_t _data_limit;  // the connection-level limit at which blocking occurred.
};

}

#endif