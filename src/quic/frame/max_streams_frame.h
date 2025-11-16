#ifndef QUIC_FRAME_MAX_STREAMS_FRAME
#define QUIC_FRAME_MAX_STREAMS_FRAME

#include <cstdint>
#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class MaxStreamsFrame:
    public IFrame {
public:
    MaxStreamsFrame(uint16_t frame_type);
    ~MaxStreamsFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetMaximumStreams(uint64_t maximum) { maximum_streams_ = maximum; }
    uint64_t GetMaximumStreams() { return maximum_streams_; }

private:
    uint8_t stream_type_;
    uint64_t maximum_streams_;  // A count of the cumulative number of streams of the corresponding type that can be opened over the lifetime of the connection.
};

}
}

#endif