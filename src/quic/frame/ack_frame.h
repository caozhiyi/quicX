#ifndef QUIC_FRAME_ACK_FRAME
#define QUIC_FRAME_ACK_FRAME

#include <vector>
#include <cstdint>

#include "quic/frame/if_frame.h"
#include "quic/frame/ack_range.h"

namespace quicx {
namespace quic {

class AckFrame: 
    public IFrame {
public:
    AckFrame();
    virtual ~AckFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetLargestAck(uint64_t ack) { largest_acknowledged_ = ack; }
    uint64_t GetLargestAck() { return largest_acknowledged_; }

    void SetAckDelay(uint32_t delay) { ack_delay_ = delay; }
    uint32_t GetAckDelay() { return ack_delay_; }

    void SetFirstAckRange(uint32_t range) { first_ack_range_ = range; }
    uint32_t GetFirstAckRange() { return first_ack_range_; }

    void AddAckRange(uint64_t gap, uint64_t range) { ack_ranges_.emplace_back(AckRange(gap, range)); }
    const std::vector<AckRange>& GetAckRange() { return ack_ranges_; }

protected:
    AckFrame(FrameType ft);

private:
    uint64_t largest_acknowledged_; // A variable-length integer representing the largest packet number the peer is acknowledging.
    uint32_t ack_delay_;            // the time delta in microseconds between when this ACK was sent and when the largest acknowledged packet.
    uint64_t first_ack_range_;      // A variable-length integer indicating the number of contiguous packets preceding the Largest Acknowledged that are being acknowledged
    // uint64_t ack_range_;         // A variable-length integer specifying the number of ACK Range fields in the frame.
    std::vector<AckRange> ack_ranges_;                 
};

class AckEcnFrame:
    public AckFrame {
public:
    AckEcnFrame();
    ~AckEcnFrame();

    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<common::IBufferRead> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    void SetEct0(uint64_t ect0) { ect_0_ = ect0; }
    uint64_t GetEct0() { return ect_0_; }

    void SetEct1(uint64_t ect1) { ect_1_ = ect1; }
    uint64_t GetEct1() { return ect_1_; }

    void SetEcnCe(uint64_t ce) { ecn_ce_ = ce; }
    uint64_t GetEcnCe() { return ecn_ce_; }

private:
    uint64_t ect_0_;    // the total number of packets received with the ECT(0) codepoint in the packet number space of the ACK frame.
    uint64_t ect_1_;    // the total number of packets received with the ECT(1) codepoint in the packet number space of the ACK frame.
    uint64_t ecn_ce_;   // the total number of packets received with the CE codepoint in the packet number space of the ACK frame.
};

}
}

#endif