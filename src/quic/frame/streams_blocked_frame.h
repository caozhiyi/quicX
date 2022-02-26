#ifndef QUIC_FRAME_STREAMS_BLOCKED_FRAME
#define QUIC_FRAME_STREAMS_BLOCKED_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StreamsBlockedFrame: public Frame {
public:
    StreamsBlockedFrame(uint16_t frame_type);
    ~StreamsBlockedFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetMaximumStreams(uint64_t max) { _maximum_streams = max; }
    uint64_t GetMaximumStreams() { return _maximum_streams; }

private:
    uint8_t _stream_type;
    uint32_t _maximum_streams;  // the stream limit at the time the frame was sent.
};

}

#endif