#ifndef QUIC_FRAME_ACK_RANGE_FRAME
#define QUIC_FRAME_ACK_RANGE_FRAME

#include <cstdint>

namespace quicx {
namespace quic {

class AckRange {
public:
    AckRange();
    AckRange(uint64_t gap, uint64_t range);
    ~AckRange();

    void SetAckRangeLength(uint64_t v) { ack_range_length_ = v; }
    uint64_t GetAckRangeLength() { return ack_range_length_; }

    void SetGap(uint64_t v) { gap_ = v; }
    uint64_t GetGap() { return gap_; }

private:
    uint64_t gap_; // A variable-length integer indicating the number of contiguous unacknowledged packets preceding the packet number one lower than the smallest in the preceding ACK Range
    uint64_t ack_range_length_; // A variable-length integer indicating the number of contiguous acknowledged packets preceding the largest packet number, as determined by the preceding Gap
};

}
}

#endif