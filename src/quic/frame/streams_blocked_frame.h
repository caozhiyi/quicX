#ifndef QUIC_FRAME_STREAMS_BLOCKED_FRAME
#define QUIC_FRAME_STREAMS_BLOCKED_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StreamsBlockedFrame: public Frame {
public:
    StreamsBlockedFrame();
    ~StreamsBlockedFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetStreamLimit(uint64_t limit) { _stream_limit = limit; }
    uint64_t GetStreamLimit() { return _stream_limit; }

private:
    uint8_t _stream_type;
    uint32_t _stream_limit;  // the stream limit at the time the frame was sent.
};

}

#endif