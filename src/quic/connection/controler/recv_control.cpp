#include <cstring>

#include "common/log/log.h"

#include "quic/connection/controler/recv_control.h"
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"

namespace quicx {
namespace quic {

RecvControl::RecvControl(std::shared_ptr<common::ITimer> timer):
    timer_(timer),
    set_timer_(false),
    max_ack_delay_(10) {
    memset(pkt_num_largest_recvd_, 0, sizeof(pkt_num_largest_recvd_));
    memset(largest_recv_time_, 0, sizeof(largest_recv_time_));
    memset(ect0_count_, 0, sizeof(ect0_count_));
    memset(ect1_count_, 0, sizeof(ect1_count_));
    memset(ce_count_, 0, sizeof(ce_count_));

    timer_task_ = common::TimerTask([this] {
        set_timer_ = false;
        if (active_send_cb_) {
            active_send_cb_();
        }
    });
}

void RecvControl::OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet) {
    common::LOG_DEBUG("RecvControl::OnPacketRecv: packet_number=%llu, frame_type_bit=%u, is_ack_eliciting=%d",
        packet->GetPacketNumber(), packet->GetFrameTypeBit(), IsAckElictingPacket(packet->GetFrameTypeBit()) ? 1 : 0);

    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        common::LOG_DEBUG(
            "RecvControl::OnPacketRecv: packet %llu is not ack-eliciting, skipping", packet->GetPacketNumber());
        return;
    }

    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    uint64_t pkt_num = packet->GetPacketNumber();

    // Update largest received packet number
    if (pkt_num_largest_recvd_[ns] < pkt_num) {
        pkt_num_largest_recvd_[ns] = pkt_num;
        largest_recv_time_[ns] = time;
    }

    // Add to ACK queue
    wait_ack_packet_numbers_[ns].insert(pkt_num);
    common::LOG_DEBUG("RecvControl::OnPacketRecv: added packet %llu to ACK queue, ns=%d, queue size=%zu", pkt_num, ns,
        wait_ack_packet_numbers_[ns].size());

    // RFC 9000: Determine if immediate ACK is required
    uint8_t ecn = 0;  // TODO: Get from packet header when ECN is implemented
    bool need_immediate_ack = ShouldSendImmediateAck(ns, pkt_num, ecn);

    if (need_immediate_ack) {
        common::LOG_DEBUG("RecvControl::OnPacketRecv: triggering immediate ACK for ns=%d", ns);
        if (immediate_ack_cb_) {
            immediate_ack_cb_(ns);
        }
    } else {
        // For Application packets, use timer-based ACK
        if (!set_timer_) {
            set_timer_ = true;
            timer_->AddTimer(timer_task_, max_ack_delay_);
        }
    }
}

void RecvControl::OnEcnCounters(uint8_t ecn, PacketNumberSpace ns) {
    // ECN codepoints per RFC: 0b00 Not-ECT, 0b10 ECT(0), 0b01 ECT(1), 0b11 CE
    switch (ecn & 0x03) {
        case 0x02:  // ECT(0)
            ++ect0_count_[ns];
            break;
        case 0x01:  // ECT(1)
            ++ect1_count_[ns];
            break;
        case 0x03:  // CE
            ++ce_count_[ns];
            break;
        default:
            break;
    }
}

std::shared_ptr<IFrame> RecvControl::MayGenerateAckFrame(uint64_t now, PacketNumberSpace ns, bool ecn_enabled) {
    if (set_timer_) {
        timer_->RemoveTimer(timer_task_);
        set_timer_ = false;
    }

    // Build ACK ranges from contiguous runs (descending by packet number)
    auto& nums = wait_ack_packet_numbers_[ns];
    std::vector<uint64_t> acked_packets;  // Track packets included in this ACK frame

    // Limit number of ranges to prevent oversized ACK frames
    // RFC 9000 doesn't specify a hard limit, but we need to fit in MTU
    // 64 ranges is a safe upper bound (approx 64 * 16 bytes = 1KB)
    const size_t kMaxAckRanges = 64;

    if (nums.empty()) {
        common::LOG_DEBUG("RecvControl::MayGenerateAckFrame: ns=%d, no packets to ACK", ns);
        return nullptr;
    }

    common::LOG_DEBUG("RecvControl::MayGenerateAckFrame: ns=%d, generating ACK for %zu packets, largest=%llu", ns,
        nums.size(), pkt_num_largest_recvd_[ns]);

    // Collect runs as [high, low]
    std::vector<std::pair<uint64_t, uint64_t>> runs;
    auto rit = nums.rbegin();
    uint64_t run_high = *rit;
    uint64_t run_low = run_high;
    acked_packets.push_back(*rit);
    ++rit;

    for (; rit != nums.rend(); ++rit) {
        uint64_t pn = *rit;
        if (pn + 1 == run_low) {
            // still contiguous downward
            run_low = pn;
            acked_packets.push_back(pn);
        } else {
            // close current run and start a new one
            runs.emplace_back(run_high, run_low);

            if (runs.size() >= kMaxAckRanges) {
                // Stop collecting ranges if we hit the limit
                // The remaining packets will be ACKed in the next frame
                common::LOG_WARN(
                    "RecvControl::MayGenerateAckFrame: hit max ACK ranges limit (%zu), deferring remaining ACKs",
                    kMaxAckRanges);
                break;
            }

            run_high = pn;
            run_low = pn;
            acked_packets.push_back(pn);
        }
    }
    // push last run if we haven't hit the limit
    if (runs.size() < kMaxAckRanges) {
        runs.emplace_back(run_high, run_low);
    }

    // Generate ACK or ACK_ECN frame based on ECN enable
    std::shared_ptr<AckFrame> frame;
    if (ecn_enabled) {
        auto f = std::make_shared<AckEcnFrame>();
        f->SetEct0(ect0_count_[ns]);
        f->SetEct1(ect1_count_[ns]);
        f->SetEcnCe(ce_count_[ns]);
        frame = f;
    } else {
        frame = std::make_shared<AckFrame>();
    }

    // Largest Acknowledged is the highest packet number in the limited selection (first run's high)
    uint64_t largest_ack_in_frame = runs[0].first;
    frame->SetLargestAck(largest_ack_in_frame);

    // encode ACK Delay in units of 2^ack_delay_exponent per RFC 9000
    // Use the time of the largest acknowledged packet in this frame
    // Note: We use largest_recv_time_[ns] which corresponds to pkt_num_largest_recvd_[ns]
    // If the largest in frame is less than the overall largest, we approximate the delay
    {
        uint64_t delay_ms = now - largest_recv_time_[ns];
        uint64_t encoded = delay_ms >> ack_delay_exponent_;
        frame->SetAckDelay(static_cast<uint32_t>(encoded));
    }

    // First ACK Range is size of first run minus 1
    uint64_t first_ack_range = runs[0].first - runs[0].second;
    frame->SetFirstAckRange(static_cast<uint32_t>(first_ack_range));

    // Additional ranges: Gap and Range Len (len minus 1)
    for (size_t i = 1; i < runs.size(); ++i) {
        uint64_t prev_low = runs[i - 1].second;
        uint64_t next_high = runs[i].first;
        uint64_t gap = (prev_low - next_high) - 1;
        uint64_t range_len = runs[i].first - runs[i].second;
        frame->AddAckRange(gap, range_len);
    }

    // Remove ONLY the packets that were actually included in this ACK frame
    for (uint64_t pn : acked_packets) {
        wait_ack_packet_numbers_[ns].erase(pn);
    }

    common::LOG_DEBUG("RecvControl::MayGenerateAckFrame: generated ACK for %zu packets, remaining in queue: %zu",
        acked_packets.size(), wait_ack_packet_numbers_[ns].size());

    return frame;
}

void RecvControl::UpdateConfig(const TransportParam& tp) {
    max_ack_delay_ = tp.GetMaxAckDelay();
    ack_delay_exponent_ = tp.GetackDelayExponent();
}

// RFC 9000 Section 13.2.1: Determine if immediate ACK is required
bool RecvControl::ShouldSendImmediateAck(PacketNumberSpace ns, uint64_t pkt_num, uint8_t ecn) {
    // RFC 9000: Initial and Handshake packets MUST be ACKed immediately
    if (ns == kInitialNumberSpace || ns == kHandshakeNumberSpace) {
        common::LOG_DEBUG("ShouldSendImmediateAck: Initial/Handshake packet, immediate ACK required");
        return true;
    }

    // RFC 9000 Section 13.2.1: ECN CE packets SHOULD be ACKed immediately
    if ((ecn & 0x03) == 0x03) {  // CE codepoint = 0b11
        common::LOG_DEBUG("ShouldSendImmediateAck: ECN CE packet, immediate ACK");
        return true;
    }

    auto& acked_packets = wait_ack_packet_numbers_[ns];

    // RFC 9000: Immediate ACK if packet number < previously received packet (out of order)
    if (pkt_num < pkt_num_largest_recvd_[ns]) {
        common::LOG_DEBUG("ShouldSendImmediateAck: Out-of-order (pkt=%llu < largest=%llu), immediate ACK", pkt_num,
            pkt_num_largest_recvd_[ns]);
        return true;
    }

    // RFC 9000: Immediate ACK if packet number > largest but there are gaps
    if (pkt_num > pkt_num_largest_recvd_[ns] + 1) {
        common::LOG_DEBUG("ShouldSendImmediateAck: Gap detected (expected=%llu, got=%llu), immediate ACK",
            pkt_num_largest_recvd_[ns] + 1, pkt_num);
        return true;
    }

    // RFC 9000 Section 13.2.2: Send ACK after at least 2 ack-eliciting packets
    if (acked_packets.size() >= 2) {
        common::LOG_DEBUG("ShouldSendImmediateAck: 2+ packets in queue, sending ACK");
        return true;
    }

    return false;  // Can delay ACK
}

// RFC 9000 Section 4.10: Discard packet number space state
void RecvControl::DiscardPacketNumberSpace(PacketNumberSpace ns) {
    wait_ack_packet_numbers_[ns].clear();
    pkt_num_largest_recvd_[ns] = 0;
    largest_recv_time_[ns] = 0;
    ect0_count_[ns] = 0;
    ect1_count_[ns] = 0;
    ce_count_[ns] = 0;
    common::LOG_INFO("RecvControl: Discarded packet number space %d per RFC 9000", ns);
}

}  // namespace quic
}  // namespace quicx
