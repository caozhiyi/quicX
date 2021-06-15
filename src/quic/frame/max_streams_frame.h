#ifndef QUIC_FRAME_MAX_STREAMS_FRAME
#define QUIC_FRAME_MAX_STREAMS_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class MaxStreamsFrame: public Frame {
public:
    MaxStreamsFrame();
    ~MaxStreamsFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetMaximumStreams(uint64_t maximum) { _maximum_streams = maximum; }
    uint64_t GetMaximumStreams() { return _maximum_streams; }

private:
    uint8_t _stream_type;
    uint64_t _maximum_streams;  // A count of the cumulative number of streams of the corresponding type that can be opened over the lifetime of the connection.
};

}

#endif