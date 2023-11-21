#ifndef QUIC_FRAME_STREAMS_BLOCKED_FRAME
#define QUIC_FRAME_STREAMS_BLOCKED_FRAME

#include <cstdint>
#include "quic/frame/stream_frame_interface.h"

namespace quicx {
namespace quic {

class StreamsBlockedFrame:
    public IFrame {
public:
    StreamsBlockedFrame(uint16_t frame_type);
    ~StreamsBlockedFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetMaximumStreams(uint64_t max) { _maximum_streams = max; }
    uint64_t GetMaximumStreams() { return _maximum_streams; }

private:
    uint32_t _maximum_streams;  // the stream limit at the time the frame was sent.
};

}
}

#endif