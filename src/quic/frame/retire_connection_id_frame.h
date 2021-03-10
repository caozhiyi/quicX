#ifndef QUIC_FRAME_RETIRE_CONNECTION_ID_FRAME
#define QUIC_FRAME_RETIRE_CONNECTION_ID_FRAME

#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class RetireConnectionIDFrame : public Frame {
public:
    RetireConnectionIDFrame();
    ~RetireConnectionIDFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetSequenceNumber(uint64_t num) { _sequence_number = num; }
    uint64_t GetSequenceNumber() { return _sequence_number; }

private:
    uint64_t _sequence_number;  // the connection ID being retired.
};

}

#endif