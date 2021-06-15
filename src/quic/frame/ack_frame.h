#ifndef QUIC_FRAME_ACK_FRAME
#define QUIC_FRAME_ACK_FRAME

#include <vector>
#include <cstdint>

#include "ack_range.h"
#include "frame_interface.h"

namespace quicx {

class AckFrame: 
    public Frame {
public:
    AckFrame();
    virtual ~AckFrame();

    bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter, bool with_type = false);
    uint32_t EncodeSize();

    void SetAckDelay(uint32_t delay) { _ack_delay = delay; }
    uint32_t GetAckDelay() { return _ack_delay; }

    void AddAckRange(uint64_t smallest, uint64_t largest) { _ack_ranges.emplace_back(AckRange(smallest, largest)); }
    const std::vector<AckRange>& GetAckRange() { return _ack_ranges; }

protected:
    AckFrame(FrameType ft);

private:
    uint32_t _ack_delay;       // the time delta in microseconds between when this ACK was sent and when the largest acknowledged packet.
    /*
    uint32_t _ack_range_count; // the number of Gap and ACK Range fields in the frame.
    AckRange *_ack_ranges      // ranges of packets which are alternately not acknowledged (Gap) and acknowledged (ACK Range).
    */
    std::vector<AckRange> _ack_ranges;                 
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