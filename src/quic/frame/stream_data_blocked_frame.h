#ifndef QUIC_FRAME_STREAM_DATA_BLOCKED_FRAME
#define QUIC_FRAME_STREAM_DATA_BLOCKED_FRAME

#include <cstdint>
#include "quic/frame/if_stream_frame.h"

namespace quicx {
namespace quic {

class StreamDataBlockedFrame:
    public IStreamFrame {
public:
    StreamDataBlockedFrame();
    ~StreamDataBlockedFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetMaximumData(uint64_t max) { maximum_data_ = max; }
    uint64_t GetMaximumData() { return maximum_data_; }

private:
   uint64_t maximum_data_;  // the connection-level limit at which blocking occurred.
};

}
}

#endif