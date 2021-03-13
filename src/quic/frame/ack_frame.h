#ifndef QUIC_FRAME_ACK_FRAME
#define QUIC_FRAME_ACK_FRAME

#include <vector>
#include <cstdint>
#include "frame_interface.h"

namespace quicx {

struct AckRange {
    uint32_t _gap;         // the number of contiguous unacknowledged packets preceding the packet number one lower than the smallest in the preceding ACK Range.
    uint32_t _ack_range;   // the number of contiguous acknowledged packets preceding the largest packet number.
};

class AckFrame: public Frame {
public:
    AckFrame();
    ~AckFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetLargestAck(uint64_t ack) { _largest_ack = ack; }
    uint64_t GetLargestAck() { return _largest_ack; }

    void SetAckDelay(uint32_t delay) { _ack_delay = delay; }
    uint32_t GetAckDelay() { return _ack_delay; }

    void SetFirstAckRange(uint32_t range) { _first_ack_range = range; }
    uint32_t GetFirstAckRange() { return _first_ack_range; }

    void AddAckRange(uint32_t gap, uint32_t range) { _ack_ranges.emplace_back(AckRange{gap, range}); }
    const std::vector<AckRange>& GetAckRange() { return _ack_ranges; }

protected:
    AckFrame(FrameType ft);

private:
    uint64_t _largest_ack;     // largest packet number the peer is acknowledging.
    uint32_t _ack_delay;       // the time delta in microseconds between when this ACK was sent and when the largest acknowledged packet.
    /*
    uint32_t _ack_range_count; // the number of Gap and ACK Range fields in the frame.
    AckRange *_ack_ranges      // ranges of packets which are alternately not acknowledged (Gap) and acknowledged (ACK Range).
    */
    uint32_t _first_ack_range; // the number of contiguous packets preceding the Largest Acknowledged that are being acknowledged.
    std::vector<AckRange> _ack_ranges;

    /*0    1    2    3    4    5    6    7    8    9    10    11    12    13    14     15    16    17    18    19   20 */
    /*          <range:3  gap:4>         <range:4          gap:4>               <first_ack_range:6      largest_ack:20>*/                   
};


class AckEcnFrame: public AckFrame {
public:
    AckEcnFrame();
    ~AckEcnFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetEct0(uint64_t ect0) { _ect_0 = ect0; }
    uint64_t GetEct0() { return _ect_0; }

    void SetEct1(uint64_t ect1) { _ect_1 = ect1; }
    uint64_t GetEct1() { return _ect_1; }

    void SetEcnCe(uint64_t ce) { _ecn_ce = ce; }
    uint64_t GetEcnCe() { return _ecn_ce; }

private:
    uint64_t _ect_0;    // the total number of packets received with the ECT(0) codepoint in the packet number space of the ACK frame.
    uint64_t _ect_1;    // the total number of packets received with the ECT(1) codepoint in the packet number space of the ACK frame.
    uint64_t _ecn_ce;   // the total number of packets received with the CE codepoint in the packet number space of the ACK frame.
};

}

#endif