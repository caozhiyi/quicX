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

    void SetDataLimit(uint64_t limit) { _data_limit = limit; }
    uint64_t GetDataLimit() { return _data_limit; }

private:
   uint64_t _data_limit;  //  the connection-level limit at which blocking occurred.
};

}

#endif