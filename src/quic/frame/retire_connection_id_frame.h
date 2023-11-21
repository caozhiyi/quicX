#ifndef QUIC_FRAME_RETIRE_CONNECTION_ID_FRAME
#define QUIC_FRAME_RETIRE_CONNECTION_ID_FRAME

#include <cstdint>
#include "quic/frame/frame_interface.h"

namespace quicx {
namespace quic {

class RetireConnectionIDFrame:
    public IFrame {
public:
    RetireConnectionIDFrame();
    ~RetireConnectionIDFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetSequenceNumber(uint64_t num) { _sequence_number = num; }
    uint64_t GetSequenceNumber() { return _sequence_number; }

private:
    uint64_t _sequence_number;  // the connection ID being retired.
};

}
}

#endif