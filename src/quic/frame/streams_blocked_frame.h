#ifndef QUIC_FRAME_STREAMS_BLOCKED_FRAME
#define QUIC_FRAME_STREAMS_BLOCKED_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StreamsBlockedFrame : public Frame {
public:
    StreamsBlockedFrame() : Frame(FT_STREAMS_BLOCKED) {}
    ~StreamsBlockedFrame() {}

private:
   uint32_t _stream_limit;  // the stream limit at the time the frame was sent.
};

}

#endif