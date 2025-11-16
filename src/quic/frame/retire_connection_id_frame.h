#ifndef QUIC_FRAME_RETIRE_CONNECTION_ID_FRAME
#define QUIC_FRAME_RETIRE_CONNECTION_ID_FRAME

#include <cstdint>
#include "quic/frame/if_frame.h"

namespace quicx {
namespace quic {

class RetireConnectionIDFrame:
    public IFrame {
public:
    RetireConnectionIDFrame();
    ~RetireConnectionIDFrame();

    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool Decode(std::shared_ptr<common::IBuffer> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetSequenceNumber(uint64_t num) { sequence_number_ = num; }
    uint64_t GetSequenceNumber() { return sequence_number_; }

private:
    uint64_t sequence_number_;  // the connection ID being retired.
};

}
}

#endif