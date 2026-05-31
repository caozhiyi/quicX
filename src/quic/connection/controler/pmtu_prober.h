#ifndef QUIC_CONNECTION_CONTROLER_PMTU_PROBER
#define QUIC_CONNECTION_CONTROLER_PMTU_PROBER

#include <cstdint>
#include <memory>

namespace quicx {
namespace quic {

class IFrame;
class AckFrame;

// PmtuProber encapsulates Path MTU Discovery (PMTUD) logic.
// Responsible for probing a larger MTU after migration and detecting probe
// success via ACK range analysis. Extracted from SendManager for single
// responsibility.
class PmtuProber {
public:
    PmtuProber();
    ~PmtuProber() = default;

    // Start a PMTU probe sequence. Picks a target MTU slightly above the
    // current limit and marks the probe as in-flight.
    void StartProbe();

    // Notify probe result (success selects the higher MTU, failure falls back).
    void OnProbeResult(bool success);

    // Reset probing state for a new path (use conservative size until probed).
    void ResetForNewPath();

    // Check whether an ACK frame covers the probe packet number. If so, calls
    // OnProbeResult(true) internally and returns true.
    bool CheckAckCoversProbe(std::shared_ptr<IFrame> ack_frame);

    // Record the packet number used for the probe packet. Should be called by
    // SendManager when the probe packet is actually sent.
    void SetProbePacketNumber(uint64_t pn) { probe_packet_number_ = pn; }

    // Current effective MTU limit.
    uint16_t GetMtuLimit() const { return mtu_limit_bytes_; }

    // Whether a probe is currently in-flight.
    bool IsProbeInflight() const { return probe_inflight_; }

    // Target MTU for the current probe.
    uint16_t GetProbeTargetBytes() const { return probe_target_bytes_; }

private:
    bool probe_inflight_{false};
    uint16_t mtu_limit_bytes_{1450};
    uint16_t probe_target_bytes_{1450};
    uint64_t probe_packet_number_{0};
};

}  // namespace quic
}  // namespace quicx

#endif