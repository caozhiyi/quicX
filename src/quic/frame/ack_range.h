#ifndef QUIC_FRAME_ACK_RANGE_FRAME
#define QUIC_FRAME_ACK_RANGE_FRAME

#include <cstdint>

namespace quicx {

class AckRange {
public:
    AckRange();
    AckRange(uint64_t gap, uint64_t range);
    ~AckRange();

    void SetAckRangeLength(uint64_t v) { _ack_range_length = v; }
    uint64_t GetAckRangeLength() { return _ack_range_length; }

    void SetGap(uint64_t v) { _gap = v; }
    uint64_t GetGap() { return _gap; }

private:
    uint64_t _gap; // A variable-length integer indicating the number of contiguous unacknowledged packets preceding the packet number one lower than the smallest in the preceding ACK Range
    uint64_t _ack_range_length; // A variable-length integer indicating the number of contiguous acknowledged packets preceding the largest packet number, as determined by the preceding Gap
};

}

#endif