#ifndef QUIC_FRAME_DATA_BLOCKED_FRAME
#define QUIC_FRAME_DATA_BLOCKED_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class DataBlockedFrame: public Frame {
public:
    DataBlockedFrame();
    ~DataBlockedFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetMaximumData(uint64_t max) { _maximum_data = max; }
    uint64_t GetMaximumData() { return _maximum_data; }

private:
   uint64_t _maximum_data;  //  the connection-level limit at which blocking occurred.
};

}

#endif