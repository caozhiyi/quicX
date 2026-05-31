#include <algorithm>

#include "common/log/log.h"
#include "quic/connection/controler/pmtu_prober.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/type.h"

namespace quicx {
namespace quic {

PmtuProber::PmtuProber()
    : probe_inflight_(false),
      mtu_limit_bytes_(1450),
      probe_target_bytes_(1450),
      probe_packet_number_(0) {
}

void PmtuProber::StartProbe() {
    if (mtu_limit_bytes_ < 1450) {
        probe_target_bytes_ = 1450;
    } else {
        probe_target_bytes_ = static_cast<uint16_t>(std::min<int>(mtu_limit_bytes_ + 50, 1500));
    }
    probe_inflight_ = true;
}

void PmtuProber::OnProbeResult(bool success) {
    if (!probe_inflight_) return;
    if (success) {
        mtu_limit_bytes_ = probe_target_bytes_;
    }
    probe_inflight_ = false;
}

void PmtuProber::ResetForNewPath() {
    probe_inflight_ = false;
    probe_packet_number_ = 0;
    mtu_limit_bytes_ = 1200;  // RFC 9000 minimum
}

bool PmtuProber::CheckAckCoversProbe(std::shared_ptr<IFrame> frame) {
    if (!probe_inflight_ || probe_packet_number_ == 0) {
        return false;
    }
    if (frame->GetType() != FrameType::kAck && frame->GetType() != FrameType::kAckEcn) {
        return false;
    }

    auto ack = std::dynamic_pointer_cast<AckFrame>(frame);
    if (!ack) {
        return false;
    }

    uint64_t largest = ack->GetLargestAck();
    uint64_t probe = probe_packet_number_;
    if (probe > largest) {
        return false;
    }

    // First contiguous range: [largest - first_range, largest]
    uint32_t first_range = ack->GetFirstAckRange();
    if (probe >= largest - first_range && probe <= largest) {
        OnProbeResult(true);
        return true;
    }

    // Walk additional ACK ranges. Per RFC 9000 §19.3.1, gap_value is the
    // unacked-count minus one, so the next range's largest PN is
    // cursor - gap_value - 2.
    uint64_t cursor = largest - first_range;
    auto ranges = ack->GetAckRange();
    for (auto it = ranges.begin(); it != ranges.end(); ++it) {
        uint64_t range_high = cursor - it->GetGap() - 2;
        uint64_t range_low =
            (range_high >= it->GetAckRangeLength()) ? (range_high - it->GetAckRangeLength()) : 0;
        if (probe >= range_low && probe <= range_high) {
            OnProbeResult(true);
            return true;
        }
        cursor = range_low;
    }
    return false;
}

}  // namespace quic
}  // namespace quicx