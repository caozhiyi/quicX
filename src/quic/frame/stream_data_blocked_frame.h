#ifndef QUIC_FRAME_STREAM_DATA_BLOCKED_FRAME
#define QUIC_FRAME_STREAM_DATA_BLOCKED_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class StreamDataBlockedFrame: public IFrame {
public:
    StreamDataBlockedFrame();
    ~StreamDataBlockedFrame();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

    void SetMaximumData(uint64_t max) { _maximum_data = max; }
    uint64_t GetMaximumData() { return _maximum_data; }

private:
   uint64_t _stream_id;     // the stream which is flow control blocked.
   uint64_t _maximum_data;  // the connection-level limit at which blocking occurred.
};

}

#endif