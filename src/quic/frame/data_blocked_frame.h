#ifndef QUIC_FRAME_DATA_BLOCKED_FRAME
#define QUIC_FRAME_DATA_BLOCKED_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class DataBlockedFrame: public IFrame {
public:
    DataBlockedFrame();
    ~DataBlockedFrame();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetMaximumData(uint64_t max) { _maximum_data = max; }
    uint64_t GetMaximumData() { return _maximum_data; }

private:
   uint64_t _maximum_data;  //  the connection-level limit at which blocking occurred.
};

}

#endif