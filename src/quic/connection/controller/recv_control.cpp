#include <algorithm>
#include <cstring>
#include <quicx/common/metrics.h>

#include "common/log/log.h"
#include "common/metrics/metrics_std.h"

#include "quic/config.h"
#include "quic/connection/controller/recv_control.h"
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"

namespace quicx {
namespace quic {

RecvControl::RecvControl(std::shared_ptr<common::ITimerScheduler> scheduler):
    set_timer_(false),
    scheduler_(scheduler),
    max_ack_delay_(10) {
    memset(pkt_num_largest_recvd_, 0, sizeof(pkt_num_largest_recvd_));
    memset(largest_recv_time_, 0, sizeof(largest_recv_time_));
    memset(ect0_count_, 0, sizeof(ect0_count_));
    memset(ect1_count_, 0, sizeof(ect1_count_));
    memset(ce_count_, 0, sizeof(ce_count_));
    for (int i = 0; i < PacketNumberSpace::kNumberSpaceCount; ++i) {
        ack_due_[i] = false;
    }
}

void RecvControl::OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet) {
    LOG_DEBUG("RecvControl::OnPacketRecv: packet_number=%llu, frame_type_bit=%u, is_ack_eliciting=%d",
        packet->GetPacketNumber(), packet->GetFrameTypeBit(), IsAckElictingPacket(packet->GetFrameTypeBit()) ? 1 : 0);

    // Metrics: Packet received
    Metrics::CounterInc(common::MetricsStd::QuicPacketsRx);

    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    uint64_t pkt_num = packet->GetPacketNumber();
    bool is_ack_eliciting = IsAckElictingPacket(packet->GetFrameTypeBit());

    // Update largest received packet number
    if (pkt_num_largest_recvd_[ns] < pkt_num) {
        pkt_num_largest_recvd_[ns] = pkt_num;
        largest_recv_time_[ns] = time;
    }

    // RFC 9000 §13.2: Track ALL received packets (including ACK-only / non-
    // ack-eliciting ones) in the ACK ranges so the next outgoing ACK frame
    // reports the full set of received packet numbers. This is essential for
    // corruption recovery: when the peer's Finished (an ack-eliciting packet)
    // is lost but subsequent ACK-only packets arrive, the gap in our ACK
    // ranges tells the peer exactly which packet to retransmit.
    //
    // CRITICAL (interop integration regression, 2026-09-01): ACK-only packets
    // must NOT, by themselves, make a packet number space "ack due". Only
    // ack-eliciting packets carry that obligation (RFC 9000 §13.2). The
    // Initial/Handshake MUST-ACK-immediately path keys on the *eliciting*
    // set below; keying it on the combined set made two quicx endpoints ACK
    // each other's ACK-only packets in an unbounded ACK-of-ACK ping-pong,
    // starving the application-space ACK until the peer's PTO retransmitted
    // the whole response (duplicate data+FIN delivery → double response
    // callback in HTTP/3).
    wait_ack_packet_numbers_[ns].insert(pkt_num);
    if (is_ack_eliciting) {
        wait_ack_eliciting_[ns].insert(pkt_num);
        // A genuinely ack-eliciting packet resets the bounded-reply budget:
        // normal traffic (peer retransmissions, probes) means the peer is
        // making progress and future ACK-only packets deserve fresh replies.
        ack_only_replies_[ns] = 0;
        ack_only_seq_[ns] = 0;
    } else {
        // Non-ack-eliciting packet. RFC 9000 §13.2: it carries no ACK
        // obligation — keying the MUST-ACK path on it made two quicx
        // endpoints ACK each other's ACK-only packets in an unbounded
        // ping-pong (integration regression 2026-09-01: the war dragged the
        // scheduler to Handshake level and starved the application ACK
        // until the peer PTO-retransmitted the whole response).
        //
        // BUT: in Initial/Handshake spaces a bounded reply is the ONLY
        // recovery channel for peers that, after losing their Finished, keep
        // sending ACK-only handshake packets and never PING-probe there
        // (observed: quinn handshakeloss — our reply's ACK ranges expose the
        // gap at the lost Finished, which triggers its retransmission).
        // Cap the replies per space so a quicx↔quicx exchange self-terminates
        // (~2x kMaxAckOnlyReplies packets) instead of looping forever.
        if (ns == kInitialNumberSpace || ns == kHandshakeNumberSpace) {
            if (ack_only_replies_[ns] < kMaxAckOnlyReplies) {
                ack_only_replies_[ns]++;
                LOG_DEBUG("RecvControl::OnPacketRecv: replying to ACK-only packet %llu in ns=%d (%u/%u)",
                    (unsigned long long)pkt_num, ns, ack_only_replies_[ns], kMaxAckOnlyReplies);
                if (immediate_ack_cb_) {
                    immediate_ack_cb_(ns);
                }
            }
        } else if (ns == kApplicationNumberSpace) {
            // RFC 9000 §13.2.1 para 6: acknowledge at least every second
            // consecutive ACK-only packet. Rationale (aioquic corruption
            // runs, 2026-09-03): when the client's 1-RTT request datagram
            // is corrupted, PTO-only clients retransmit no stream data and
            // instead emit ACK-only / PING probes; our ACK (whose ranges
            // expose the gap at the lost request pn) is the only signal
            // that lets them declare the request lost and retransmit it.
            // PING probes already get the immediate duplicate-ACK path
            // above; this covers peers whose probes are pure ACK frames.
            // Routing: through the regular send loop (ack_due_ +
            // active_send_cb_), NOT the synchronous immediate path —
            // high-rate synchronous sends from the recv stack race the
            // worker's send loop (see P0 note in OnPacketRecv).
            // Self-terminating: any ack-eliciting packet resets the
            // counter, so this never fires during normal traffic.
            ack_only_seq_[ns]++;
            if (ack_only_seq_[ns] >= kAckOnlySeqThreshold) {
                ack_only_seq_[ns] = 0;
                LOG_INFO(
                    "RecvControl::OnPacketRecv: %u consecutive ACK-only packets in ns=%d, sending anti-deadlock ACK",
                    kAckOnlySeqThreshold, ns);
                ack_due_[ns] = true;
                if (active_send_cb_) {
                    active_send_cb_();
                }
            }
        }
        LOG_DEBUG("RecvControl::OnPacketRecv: packet %llu is not ack-eliciting, tracking in ranges only", pkt_num);
        return;
    }
    LOG_DEBUG("RecvControl::OnPacketRecv: added packet %llu to ACK queue, ns=%d, queue size=%zu", pkt_num, ns,
        wait_ack_packet_numbers_[ns].size());

    // RFC 9000: Determine if immediate ACK is required.
    //
    // ECN is intentionally hard-wired to 0 here (Not-ECT). End-to-end ECN
    // would require plumbing the per-packet codepoint through NetPacket →
    // IPacket all the way into RecvControl: udp_receiver.cpp already pulls
    // it via RecvFromWithEcn() / IP_TOS cmsg on Linux, but the packet-level
    // hand-off does not yet carry it. Since ECN-CE only changes ACK
    // *timing* (one of several immediate-ACK triggers in
    // ShouldSendImmediateAck), and the threshold / OoO / gap / handshake
    // triggers already cover the correctness-relevant cases, treating
    // every received packet as Not-ECT here is safe — at worst we lose
    // ECN-driven congestion-response responsiveness. Tracked as a
    // learning-only limitation in learning_project_roadmap.md §2.
    uint8_t ecn = 0;
    // Probe-triggered immediate ACK (L1 handshakeloss evidence, 30% loss each
    // direction): when the client's 1-RTT request datagram is lost, quinn and
    // s2n-quic stop retransmitting the stream data and instead probe with
    // PING-only 1-RTT packets (PTO). Our ACK is then the only signal that
    // tells the peer the request pn is missing (the ACK gap triggers its
    // retransmission). Through the normal delayed-ACK path that reply is a
    // single ~35-byte datagram; if it is also lost, the peer keeps probing
    // blind and the connection dies at idle timeout with a 0-byte transfer.
    // Route PING probes through the synchronous immediate-ACK path, which
    // also emits a second copy (see SendImmediateAck in connection_base.cpp):
    // delivery probability goes ~70% -> ~91% per probe. Probes are rare (one
    // per PTO cycle with exponential backoff), so the synchronous send from
    // the recv stack does not reintroduce the high-rate ACK race noted below.
    bool is_ping_probe = (packet->GetFrameTypeBit() & FrameTypeBit::kPingBit) != 0;
    // Handshake-confirmation window control frames (NEW_CONNECTION_ID) get the
    // same immediate duplicate-ACK treatment. Evidence (L1 conn 58403, run
    // logs_l1_pingfix_1): quinn queues 2 NCID frames right after its Finished
    // and they sit AHEAD of stream data (the GET) in quinn's transmit
    // priority; each PTO cycle is capped at 2 probe packets, so until the
    // NCIDs are ACKed the GET is never (re)transmitted. When our single
    // delayed ACK for those NCIDs is lost, the next PTO re-sends NCIDs again
    // (observed twice, both ACKs lost) and the request starves until the test
    // timeout. Acking NCID-bearing packets immediately with a duplicate copy
    // (~70% -> ~91% delivery) clears the client queue fast so the pending
    // request goes out on the next PTO. NCID frames only appear in the early
    // post-handshake window, so this never affects steady-state throughput.
    bool is_ncid = (packet->GetFrameTypeBit() & FrameTypeBit::kNewConnectionIdBit) != 0;
    if (is_ping_probe || is_ncid) {
        LOG_INFO(
            "RecvControl::OnPacketRecv: control frame probe (pn=%llu, ns=%d, ping=%d ncid=%d) -> immediate duplicate "
            "ACK",
            pkt_num, ns, is_ping_probe ? 1 : 0, is_ncid ? 1 : 0);
    }
    bool need_immediate_ack = is_ping_probe || is_ncid || ShouldSendImmediateAck(ns, pkt_num, ecn);

    if (need_immediate_ack) {
        LOG_DEBUG("RecvControl::OnPacketRecv: triggering immediate ACK for ns=%d", ns);
        // PERF FIX (P0): mark this space as ack-due so the scheduler's
        // ShouldSendAckNow(ns) returns true on the next TrySend(). Before
        // this fix, any non-empty wait queue caused the next outgoing
        // datagram to carry an ACK, which short-circuited the threshold
        // and timer logic and produced a 1:1 packet/ACK ratio.
        ack_due_[ns] = true;

        // PERF FIX (P0 follow-up): Initial / Handshake spaces still need
        // the synchronous out-of-band immediate-ACK path, because the
        // worker may not yet be processing 1-RTT data and missing those
        // ACKs delays handshake completion. For Application data we route
        // the ACK through the regular TrySend() loop instead — high-rate
        // synchronous SendImmediateAck() from inside the recv stack
        // raced with the worker's own send loop and produced a
        // tail-of-transfer deadlock under sustained upload.
        if (ns == kInitialNumberSpace || ns == kHandshakeNumberSpace || is_ping_probe || is_ncid) {
            if (immediate_ack_cb_) {
                immediate_ack_cb_(ns);
            }
        } else {
            // Application: just kick the send loop; it will pick up the
            // ack_due_ flag via ShouldSendAckNow() and emit the ACK
            // attached to the next outgoing datagram (or on its own).
            if (active_send_cb_) {
                active_send_cb_();
            }
        }
    } else {
        // For Application packets, use timer-based ACK
        if (!set_timer_) {
            set_timer_ = true;
            ArmAckDelayTimer();
        }
        Metrics::CounterInc(common::MetricsStd::DiagRecvAckDelayed);
    }
}

void RecvControl::ArmAckDelayTimer() {
    // Rearm reuses the existing timer node, which matters because a delayed ACK
    // is (re)armed for nearly every application packet received.
    if (ack_delay_timer_.Rearm(max_ack_delay_)) {
        return;
    }
    if (!scheduler_) {
        return;
    }
    ack_delay_timer_ = scheduler_->AddTimer(
        life_token_,
        [this] {
            set_timer_ = false;
            // PERF FIX (P0): The max_ack_delay_ timer only ever fires for
            // Application-space packets (Initial/Handshake go through the
            // immediate-ACK path in OnPacketRecv and never schedule this timer).
            // Mark the Application space as ack-due so the next TrySend() will
            // actually flush the aggregated ACK frame.
            ack_due_[kApplicationNumberSpace] = true;
            if (active_send_cb_) {
                active_send_cb_();
            }
        },
        max_ack_delay_);
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
    Metrics::CounterInc(common::MetricsStd::DiagAckGenCalls);
    if (set_timer_) {
        ack_delay_timer_.Cancel();
        set_timer_ = false;
    }

    // PERF FIX (P0): clear the ack-due flag now; any remaining packets in
    // the wait queue (e.g. after kMaxAckRanges truncation) will re-arm the
    // flag via the threshold check in ShouldSendImmediateAck(), the
    // max_ack_delay_ timer, or an OoO/gap trigger from a new arrival.
    ack_due_[ns] = false;

    // Build ACK ranges from contiguous runs (descending by packet number)
    auto& nums = wait_ack_packet_numbers_[ns];
    std::vector<uint64_t> acked_packets;  // Track packets included in this ACK frame

    // Limit number of ranges to prevent oversized ACK frames
    // RFC 9000 doesn't specify a hard limit, but we need to fit in MTU
    // 64 ranges is a safe upper bound (approx 64 * 16 bytes = 1KB)
    if (nums.empty()) {
        return nullptr;
    }
    Metrics::CounterInc(common::MetricsStd::DiagAckGenEmitted);
    Metrics::CounterInc(common::MetricsStd::DiagAckQueueDepth, nums.size());

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
                LOG_WARN("RecvControl::MayGenerateAckFrame: hit max ACK ranges limit (%u), deferring remaining ACKs",
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

    // BUGFIX P1-3: ACK Delay should reflect the time since receiving the
    // largest acknowledged packet *in this frame*, not the global largest.
    // When ACK ranges are truncated (kMaxAckRanges limit), largest_ack_in_frame
    // may differ from pkt_num_largest_recvd_[ns].
    // Since we only track time for the overall largest, use it when they match.
    // When they differ, use 0 delay to avoid reporting a misleadingly small value
    // that would cause the peer to underestimate RTT.
    {
        uint64_t delay_ms = 0;
        if (largest_ack_in_frame == pkt_num_largest_recvd_[ns]) {
            // Exact: largest in frame IS the global largest, use tracked time
            delay_ms = now - largest_recv_time_[ns];
        } else {
            // Approximate: We don't track per-packet receive time.
            // Report 0 delay so peer doesn't subtract an incorrect value from RTT.
            // RFC 9000 §13.2: "An endpoint MIGHT intentionally delay sending
            // an ACK frame... The ACK Delay field... indicates the time the
            // largest acknowledged packet was received."
            // Reporting 0 is safe — peer will slightly overestimate RTT, which
            // is conservative and better than underestimation.
            delay_ms = 0;
        }
        uint64_t encoded = delay_ms >> ack_delay_exponent_;
        frame->SetAckDelay(static_cast<uint32_t>(encoded));
    }

    // First ACK Range is size of first run minus 1
    uint64_t first_ack_range = runs[0].first - runs[0].second;
    frame->SetFirstAckRange(static_cast<uint32_t>(first_ack_range));

    // Additional ranges: Gap and Range Length encoded per RFC 9000 §19.3.1.
    //
    // For two adjacent runs runs[i-1] (smallest = prev_low) and runs[i]
    // (largest = next_high) the actual count of unacked packets between
    // them is (prev_low - 1) - (next_high + 1) + 1 = prev_low - next_high - 1.
    // RFC 9000 §19.3.1: "Each Gap field encodes the length of a sequence of
    // unacknowledged packet numbers ... as one less than its actual length."
    // Therefore gap_value = (prev_low - next_high - 1) - 1 = prev_low - next_high - 2.
    //
    // BUGFIX (G2 / Bug #22, encoder side):
    //   The previous code emitted gap = prev_low - next_high - 1, encoding one
    //   too few unacked packets. Combined with the symmetric -1 mistake on the
    //   decoder side (send_control.cpp ~line 307), peers would see selective-
    //   ACK PNs shifted by exactly 0 (the two bugs cancelled). After fixing
    //   the decoder alone, this side started telling RFC-compliant peers
    //   (e.g. quic-go) that an extra packet was acknowledged — which broke
    //   transfer in the quicx-client direction (peer reused PNs for fresh
    //   data, our SendStream tracking went out-of-order, and quic-go's server
    //   reset the connection).
    for (size_t i = 1; i < runs.size(); ++i) {
        uint64_t prev_low = runs[i - 1].second;
        uint64_t next_high = runs[i].first;
        uint64_t gap = (prev_low - next_high) - 2;
        uint64_t range_len = runs[i].first - runs[i].second;
        frame->AddAckRange(gap, range_len);
    }

    // Remove ONLY the packets that were actually included in this ACK frame
    for (uint64_t pn : acked_packets) {
        wait_ack_packet_numbers_[ns].erase(pn);
        wait_ack_eliciting_[ns].erase(pn);
    }

    LOG_DEBUG("RecvControl::MayGenerateAckFrame: generated ACK for %zu packets, remaining in queue: %zu",
        acked_packets.size(), wait_ack_packet_numbers_[ns].size());

    // PERF FIX (P0): If kMaxAckRanges truncated us and packets remain
    // unacked, re-mark the space as ack-due so the next TrySend() can
    // continue draining the queue without waiting for a fresh trigger.
    if (!wait_ack_packet_numbers_[ns].empty()) {
        ack_due_[ns] = true;
    }

    // Metrics: Track ACK frequency
    ack_count_++;
    if (last_ack_time_ > 0 && now > last_ack_time_) {
        uint64_t frequency = 1000 / (now - last_ack_time_);  // ACKs per second
        Metrics::GaugeSet(common::MetricsStd::AckFrequency, frequency);
    }
    last_ack_time_ = now;

    return frame;
}

void RecvControl::UpdateConfig(const TransportParam& tp) {
    max_ack_delay_ = static_cast<uint32_t>(tp.GetMaxAckDelay());
    // Defence in depth against a shift overflow; see SendControl::UpdateConfig.
    ack_delay_exponent_ =
        static_cast<uint32_t>(std::min<uint64_t>(tp.GetackDelayExponent(), TransportParam::kMaxAckDelayExponent));
}

// RFC 9000 Section 13.2.1: Determine if immediate ACK is required
bool RecvControl::ShouldSendImmediateAck(PacketNumberSpace ns, uint64_t pkt_num, uint8_t ecn) {
    // RFC 9000: Initial and Handshake packets MUST be ACKed immediately
    if (ns == kInitialNumberSpace || ns == kHandshakeNumberSpace) {
        LOG_DEBUG("ShouldSendImmediateAck: Initial/Handshake packet, immediate ACK required");
        Metrics::CounterInc(common::MetricsStd::DiagRecvAckInitial);
        return true;
    }

    // RFC 9000 Section 13.2.1: ECN CE packets SHOULD be ACKed immediately
    if ((ecn & 0x03) == 0x03) {  // CE codepoint = 0b11
        LOG_DEBUG("ShouldSendImmediateAck: ECN CE packet, immediate ACK");
        Metrics::CounterInc(common::MetricsStd::DiagRecvAckEcn);
        return true;
    }

    auto& acked_packets = wait_ack_packet_numbers_[ns];
    (void)acked_packets;

    // RFC 9000: Immediate ACK if packet number < previously received packet (out of order)
    if (pkt_num < pkt_num_largest_recvd_[ns]) {
        LOG_DEBUG("ShouldSendImmediateAck: Out-of-order (pkt=%llu < largest=%llu), immediate ACK", pkt_num,
            pkt_num_largest_recvd_[ns]);
        Metrics::CounterInc(common::MetricsStd::DiagRecvAckOoo);
        return true;
    }

    // RFC 9000: Immediate ACK if packet number > largest but there are gaps
    if (pkt_num > pkt_num_largest_recvd_[ns] + 1) {
        LOG_DEBUG("ShouldSendImmediateAck: Gap detected (expected=%llu, got=%llu), immediate ACK",
            pkt_num_largest_recvd_[ns] + 1, pkt_num);
        Metrics::CounterInc(common::MetricsStd::DiagRecvAckGap);
        return true;
    }

    // RFC 9000 Section 13.2.2: Send ACK after at least 2 ack-eliciting packets.
    // NOTE: RFC says "at least 2" as a *lower bound* against unbounded delay,
    // not a hard upper bound. Flushing at exactly 2 packets defeats ACK
    // aggregation (peer sees ~1 packet/ACK ratio), which on fast paths
    // (e.g. loopback) chains cwnd growth to per-packet RTT and starves
    // throughput. Use a larger threshold so we keep aggregating when packets
    // arrive faster than max_ack_delay; the timer (max_ack_delay_) still
    // bounds worst-case delay if traffic is sparse.
    // The threshold is centralized in quic/config.h::kAckThreshold so it can
    // be tuned in one place without recompiling individual call sites.
    // Count ACK-ELICITING packets only: ACK-only packets tracked in the ranges
    // do not carry an ACK obligation (RFC 9000 §13.2) and must not inflate the
    // trigger (see OnPacketRecv).
    if (wait_ack_eliciting_[ns].size() >= kAckThreshold) {
        LOG_DEBUG("ShouldSendImmediateAck: %zu+ ack-eliciting packets in queue, sending ACK", kAckThreshold);
        Metrics::CounterInc(common::MetricsStd::DiagRecvAckThreshold);
        return true;
    }

    return false;  // Can delay ACK
}

// RFC 9000 Section 4.10: Discard packet number space state
void RecvControl::DiscardPacketNumberSpace(PacketNumberSpace ns) {
    wait_ack_packet_numbers_[ns].clear();
    wait_ack_eliciting_[ns].clear();
    ack_only_replies_[ns] = 0;
    ack_only_seq_[ns] = 0;
    pkt_num_largest_recvd_[ns] = 0;
    largest_recv_time_[ns] = 0;
    ect0_count_[ns] = 0;
    ect1_count_[ns] = 0;
    ce_count_[ns] = 0;
    ack_due_[ns] = false;
    LOG_INFO("RecvControl: Discarded packet number space %d per RFC 9000", ns);
}

// PERF FIX (P0): see header for rationale.
// An ACK is only "due" if it has packets to ACK AND a trigger has fired:
//   - Initial / Handshake spaces (RFC 9000 §13.2.1: MUST ACK immediately).
//     These never go through the delayed timer in OnPacketRecv, but we
//     still gate on `ack_due_` (which is set the moment a packet arrives
//     in those spaces) to keep the contract uniform.
//   - Application space: ack_due_ is set by an immediate-ACK trigger
//     (threshold / OoO / gap / ECN-CE) or by the max_ack_delay_ timer.
bool RecvControl::ShouldSendAckNow(PacketNumberSpace ns) const {
    if (wait_ack_packet_numbers_[ns].empty()) {
        return false;
    }
    // RFC 9000 §13.2.1: Initial and Handshake packets MUST be ACKed
    // immediately. We don't defer them under any circumstance.
    // Key on the ACK-ELICITING set: ACK-only packets tracked in the ranges
    // carry no ACK obligation — keying on the combined set makes two quicx
    // endpoints ACK each other's ACK-only packets forever (ACK-of-ACK
    // ping-pong) and starves the application-space ACK (2026-09-01
    // integration regression: double response callback via full response
    // retransmission).
    if (ns == kInitialNumberSpace || ns == kHandshakeNumberSpace) {
        return !wait_ack_eliciting_[ns].empty();
    }
    return ack_due_[ns];
}

}  // namespace quic
}  // namespace quicx
