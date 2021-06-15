#ifndef QUIC_FRAME_ACK_RANGE_FRAME
#define QUIC_FRAME_ACK_RANGE_FRAME

#include <cstdint>

namespace quicx {

class AckRange {
public:
    AckRange();
    AckRange(uint64_t smallest, uint64_t largest);
    ~AckRange();

    void SetLargest(uint64_t v) { _largest = v; }
    uint64_t GetLargest() { return _largest; }

    void SetSmallest(uint64_t v) { _smallest = v; }
    uint64_t GetSmallest() { return _smallest; }

    uint16_t Length();

private:
    uint64_t _smallest;
    uint64_t _largest;
};

}

#endif