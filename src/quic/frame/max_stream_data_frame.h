#ifndef QUIC_FRAME_MAX_STREAM_DATA_FRAME
#define QUIC_FRAME_MAX_STREAM_DATA_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class MaxStreamDataFrame: public IFrame {
public:
    MaxStreamDataFrame();
    ~MaxStreamDataFrame();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetStreamID(uint64_t id) { _stream_id = id; }
    uint64_t GetStreamID() { return _stream_id; }

    void SetMaximumData(uint64_t maximum) { _maximum_data = maximum; }
    uint64_t GetMaximumData() { return _maximum_data; }

private:
    uint64_t _stream_id;     // indicating the stream ID of the stream.
    uint64_t _maximum_data;  // the maximum amount of data that can be sent on the entire connection.
};

}

#endif