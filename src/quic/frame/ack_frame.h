#ifndef QUIC_FRAME_ACK_FRAME
#define QUIC_FRAME_ACK_FRAME

#include <vector>
#include <cstdint>
#include "frame_interface.h"

namespace quicx {

class AckFrame : public Frame {
public:
    AckFrame() : Frame(FT_ACK) {}
    ~AckFrame() {}

protected:
    AckFrame(FrameType ft) : Frame(ft) {}

private:
    struct AckRange {
        uint32_t _gap;         // the number of contiguous unacknowledged packets preceding the packet number one lower than the smallest in the preceding ACK Range.
        uint32_t _ack_range;   // the number of contiguous acknowledged packets preceding the largest packet number.
    };
    uint32_t _largest_ack;     // largest packet number the peer is acknowledging.
    uint32_t _ack_delay;       // the time delta in microseconds between when this ACK was sent and when the largest acknowledged packet.
    uint32_t _ack_range_count; // the number of Gap and ACK Range fields in the frame.
    uint32_t _first_ack_range; // the number of contiguous packets preceding the Largest Acknowledged that are being acknowledged.
    std::vector<AckRange> _ack_ranges; // ranges of packets which are alternately not acknowledged (Gap) and acknowledged (ACK Range).
};

class AckEcnFrame : public AckFrame {
public:
    AckEcnFrame() : AckFrame(FT_ACK_ECN) {}
    ~AckEcnFrame() {}

private:
    uint32_t _ect_0; // the total number of packets received with the ECT(0) codepoint in the packet number space of the ACK frame.
    uint32_t _ect_1; // the total number of packets received with the ECT(1) codepoint in the packet number space of the ACK frame.
    uint32_t _ect;   // the total number of packets received with the CE codepoint in the packet number space of the ACK frame.
};


}

#endif