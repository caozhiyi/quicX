#ifndef QUIC_FRAME_STREAMS_BLOCKED_FRAME
#define QUIC_FRAME_STREAMS_BLOCKED_FRAME

#include <cstdint>
#include "quic/frame/if_stream_frame.h"

namespace quicx {
namespace quic {

class StreamsBlockedFrame:
    public IFrame {
public:
    StreamsBlockedFrame(uint16_t frame_type);
    ~StreamsBlockedFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetMaximumStreams(uint64_t max) { maximum_streams_ = max; }
    uint64_t GetMaximumStreams() { return maximum_streams_; }

private:
    uint32_t maximum_streams_;  // the stream limit at the time the frame was sent.
};

}
}

#endif