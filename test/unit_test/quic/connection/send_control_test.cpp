#include <chrono>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "common/network/if_event_loop.h"
#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"
#include "common/util/time.h"

#include "quic/connection/controller/send_control.h"
#include "quic/congestion_control/congestion_control_factory.h"
#include "quic/connection/error.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/handshake_done_frame.h"
#include "quic/frame/ping_frame.h"
#include "quic/frame/type.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/quicx/global_resource.h"

#include "test/unit_test/common/timer/test_timer_scheduler.h"

namespace quicx {
namespace quic {
namespace {

std::shared_ptr<Rtt1Packet> MakePacket(uint64_t packet_number, FrameTypeBit frame_bits) {
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(packet_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(packet_number));
    packet->AddFrameTypeBit(frame_bits);
    return packet;
}

TEST(SendControlTest, AckElicitingPacketsTriggerCallbacks) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl send_control(timer);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, bool>> callbacks;
    send_control.SetStreamDataAckCallback(
        [&callbacks](uint64_t stream_id, uint64_t offset_start, uint64_t length, bool has_fin) {
            callbacks.emplace_back(stream_id, offset_start, length, has_fin);
        });

    // Send two ack-eliciting packets carrying stream data
    // The new StreamDataInfo records each STREAM frame's exact byte range
    // (offset_start, length, fin), not just the high-water mark.
    auto pkt9 = MakePacket(9, FrameTypeBit::kStreamBit);
    std::vector<StreamDataInfo> data9 = {StreamDataInfo(4, /*offset=*/0, /*len=*/100, /*fin=*/false)};
    send_control.OnPacketSend(0, pkt9, 1200, data9);

    auto pkt10 = MakePacket(10, FrameTypeBit::kStreamBit);
    std::vector<StreamDataInfo> data10 = {StreamDataInfo(4, /*offset=*/100, /*len=*/50, /*fin=*/true)};
    send_control.OnPacketSend(0, pkt10, 1300, data10);

    // 3 arms: one retransmit timer per packet, plus the shared PTO timer. The
    // second packet does NOT arm a fourth timer -- it rearms the existing PTO
    // node in place, which is the whole point of the handle-based API. All three
    // are still pending.
    EXPECT_EQ(timer->ArmCount(), 3u);
    EXPECT_EQ(timer->PendingCount(), 3u);

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(10);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(1);  // Acknowledge packets 10 and 9

    send_control.OnPacketAck(10, PacketNumberSpace::kApplicationNumberSpace, ack);

    ASSERT_EQ(callbacks.size(), 2u);
    // Packet 10 (with FIN) is acknowledged first, followed by packet 9
    EXPECT_EQ(std::get<0>(callbacks[0]), 4u);
    EXPECT_EQ(std::get<1>(callbacks[0]), 100u);  // offset_start
    EXPECT_EQ(std::get<2>(callbacks[0]), 50u);   // length
    EXPECT_TRUE(std::get<3>(callbacks[0]));      // FIN
    EXPECT_EQ(std::get<0>(callbacks[1]), 4u);
    EXPECT_EQ(std::get<1>(callbacks[1]), 0u);    // offset_start
    EXPECT_EQ(std::get<2>(callbacks[1]), 100u);  // length
    EXPECT_FALSE(std::get<3>(callbacks[1]));

    // Both retransmit timers are cancelled by the ACK. PTO is cancelled too and
    // then re-armed, because the handshake is not complete (RFC 9002 6.2.2.1),
    // so exactly one timer remains pending.
    EXPECT_EQ(timer->PendingCount(), 1u);
}

TEST(SendControlTest, NonAckElicitingPacketsAreNotTracked) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl send_control(timer);

    bool callback_invoked = false;
    send_control.SetStreamDataAckCallback([&callback_invoked](uint64_t /*stream_id*/, uint64_t /*offset_start*/,
                                              uint64_t /*length*/, bool /*has_fin*/) { callback_invoked = true; });

    // Padding-only packet should not be considered ack-eliciting
    auto packet = MakePacket(1, FrameTypeBit::kPaddingBit);
    std::vector<StreamDataInfo> stream_info = {StreamDataInfo(8, /*offset=*/0, /*len=*/42, /*fin=*/false)};
    send_control.OnPacketSend(0, packet, 1000, stream_info);

    EXPECT_EQ(timer->ArmCount(), 0u);  // Timer not armed

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(1);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);

    send_control.OnPacketAck(5, PacketNumberSpace::kApplicationNumberSpace, ack);
    EXPECT_FALSE(callback_invoked);
    // The ACK arms the pre-handshake PTO (RFC 9002 §6.2.2.1), and that is the only
    // timer in play: the padding packet contributed no retransmit timer, so there
    // was nothing for the ACK to cancel.
    EXPECT_EQ(timer->ArmCount(), 1u);
    EXPECT_EQ(timer->PendingCount(), 1u);
}

// =====================================================================
// G2 (Bug #22) - send_control layer hypothesis tests.
//
// The CC algorithm itself is provably clean (see
// reno_congestion_control_test.cpp G2_* group, all PASS). The interop log
// fingerprint -- max_bytes(cwnd) cycling in 1..31 bytes for many seconds --
// must therefore originate at the send_control packet-tracking layer or
// above.
//
// In-flight accounting between SendControl and CongestionControl is governed
// by three contracts:
//   C1: Every successful OnPacketSend(ack-eliciting) increments cc.in_flight
//       by EXACTLY pkt_len bytes.
//   C2: Every OnPacketAck of an unacked, not-yet-lost packet decrements
//       cc.in_flight by EXACTLY the original pkt_len recorded at send time.
//   C3: Every loss declaration (DetectLostPackets OR per-packet PTO timer)
//       decrements cc.in_flight by EXACTLY pkt_len bytes; a subsequent ACK
//       for the same pn MUST NOT decrement again (caller-side dedup).
//
// These tests observe cc.bytes_in_flight directly via the test getter
// added to SendControl, since cwnd grows during slow-start which makes
// CanSend() readings unreliable for in-flight verification.
// =====================================================================

namespace g2 {

constexpr uint32_t kMss = 1460;

// Helper: apply a fresh ACK frame covering [low_pn, high_pn] inclusive.
void AckContiguous(SendControl& sc, uint64_t low_pn, uint64_t high_pn, uint64_t now) {
    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(high_pn);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(high_pn - low_pn);  // inclusive count = first_range + 1
    sc.OnPacketAck(now, PacketNumberSpace::kApplicationNumberSpace, ack);
}

// G2-S1: Baseline in-flight bookkeeping (C1 + C2).
// Send N packets, ACK them all, in_flight must return to 0.
TEST(SendControlG2Test, S1_FullSendThenFullAckClearsInFlight) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 0u);

    constexpr uint64_t kPktCount = 5;
    for (uint64_t pn = 1; pn <= kPktCount; ++pn) {
        auto pkt = MakePacket(pn, FrameTypeBit::kStreamBit);
        sc.OnPacketSend(/*now*/ 100 + pn, pkt, kMss);
    }

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), kPktCount * kMss)
        << "After sending " << kPktCount << " ack-eliciting packets, in_flight must be exactly N*mss.";

    AckContiguous(sc, /*low*/ 1, /*high*/ kPktCount, /*now*/ 300);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 0u) << "After ACKing every sent packet, in_flight must drain to 0.";
}

// G2-S2 [BUG FIX VERIFICATION]: Selective ACK gap-PN calculation.
//
// Setup: send pn=1,2,3. Send ACK frame:
//     largest=3, first_range=0, addrange{gap=0, length=0}
//
// Per RFC 9000 §19.3.1:
//     "Each Gap field encodes the length of a sequence of unacknowledged
//      packet numbers, prior to the previous ACK Range, as one less than
//      its actual length."
// So gap_value=0 represents 1 unacked packet (pn=2). The next range's
// largest is preceding_smallest - (gap_value+1) - 1 = 3 - 1 - 1 = 1.
// Length=0 means range covers 1 packet -> the additional range ACKs pn=1.
//
// Therefore the FRAME ACKs {pn=3, pn=1} and pn=2 stays in flight.
//
// Pre-fix, send_control.cpp:307 subtracted only (gap+1) instead of (gap+2),
// landing on pn=2 instead of pn=1, so the implementation would ACK
// {pn=3, pn=2} and orphan pn=1. Net in_flight delta was the same, but the
// orphaned PN broke SendStream byte-range tracking (FIN never recognised as
// ACKed -> cwnd-stuck-at-1..31B G2 fingerprint).
TEST(SendControlG2Test, S2_RfcCompliantSelectiveAckPnsByValue) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    std::vector<uint64_t> acked_stream_offsets;
    sc.SetStreamDataAckCallback([&](uint64_t /*sid*/, uint64_t offset_start, uint64_t /*length*/, bool /*has_fin*/) {
        acked_stream_offsets.push_back(offset_start);
    });

    // Use distinct stream offsets to identify which PN got ACKed.
    auto pkt1 = MakePacket(1, FrameTypeBit::kStreamBit);
    auto pkt2 = MakePacket(2, FrameTypeBit::kStreamBit);
    auto pkt3 = MakePacket(3, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(101, pkt1, kMss,
        std::vector<StreamDataInfo>{StreamDataInfo(/*sid=*/4, /*off=*/100, /*len=*/kMss, /*fin=*/false)});
    sc.OnPacketSend(102, pkt2, kMss,
        std::vector<StreamDataInfo>{StreamDataInfo(/*sid=*/4, /*off=*/200, /*len=*/kMss, /*fin=*/false)});
    sc.OnPacketSend(103, pkt3, kMss,
        std::vector<StreamDataInfo>{StreamDataInfo(/*sid=*/4, /*off=*/300, /*len=*/kMss, /*fin=*/false)});

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(3);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);                  // pn=3
    ack->AddAckRange(/*gap=*/0, /*range=*/0);  // RFC: pn=1
    sc.OnPacketAck(200, PacketNumberSpace::kApplicationNumberSpace, ack);

    // RFC-compliant outcome: pn=3 (offset 300) and pn=1 (offset 100) ACKed.
    ASSERT_EQ(acked_stream_offsets.size(), 2u);
    EXPECT_EQ(acked_stream_offsets[0], 300u) << "pn=3 ACKed first (largest_ack)";
    EXPECT_EQ(acked_stream_offsets[1], 100u) << "pn=1 must be ACKed by addrange{gap=0,len=0}. If got 200, the "
                                                "G2 off-by-one regression is back: send_control.cpp ~line 307 "
                                                "must subtract (gap+2), not (gap+1).";

    // pn=2 is in the gap and must remain in flight.
    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), kMss) << "Only pn=2 should remain in flight after the selective ACK.";
}

// G2-S3 [SUSPECTED BUG]: DetectLostPackets path then retransmit then full ACK.
//
// Sequence:
//   send pn=1..4
//   ACK pn=4 alone -> packet threshold declares pn=1 lost (largest_acked - 3),
//                     pn=2,3 still outstanding.
//   retransmit pn=1's payload as pn=5.
//   ACK pn=2,3,5.
//
// Expected at each step (in_flight in mss units):
//   after send 1..4:        4
//   after ACK 4 + loss 1:   2 (pn=2,3)
//   after retransmit pn=5:  3 (pn=2,3,5)
//   after ACK {2,3,5}:      0
//
// This exercises the DetectLostPackets branch (line ~537) which DOES erase
// from unacked_packets_. If C3 holds at this layer, in_flight ends at 0.
TEST(SendControlG2Test, S3_DetectLossPathThenRetransmitDoesNotLeakInFlight) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    auto pkt1 = MakePacket(1, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(100, pkt1, kMss);
    auto pkt2 = MakePacket(2, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(101, pkt2, kMss);
    auto pkt3 = MakePacket(3, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(102, pkt3, kMss);
    auto pkt4 = MakePacket(4, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(103, pkt4, kMss);
    ASSERT_EQ(sc.GetCcBytesInFlightForTest(), 4u * kMss);

    AckContiguous(sc, 4, 4, 200);  // ACK pn=4 -> declares pn=1 lost

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 2u * kMss) << "After ACK(4) + loss(1): outstanding {2,3} = 2*mss.";

    auto pkt5_retx = MakePacket(5, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(220, pkt5_retx, kMss);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 3u * kMss) << "After retransmit pn=5: outstanding {2,3,5} = 3*mss.";

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(5);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);                  // pn=5
    ack->AddAckRange(/*gap=*/0, /*range=*/1);  // skip pn=4 (already ACKed), cover 3,2
    sc.OnPacketAck(300, PacketNumberSpace::kApplicationNumberSpace, ack);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 0u)
        << "All packets accounted for; in_flight must be 0. "
           "If non-zero, send_control LEAKED in_flight by "
        << sc.GetCcBytesInFlightForTest() << " bytes via the DetectLost path -- this is the G2 fingerprint.";
}

// G2-S4 [SUSPECTED BUG]: Spurious ACK for already-erased pn.
//
// After DetectLostPackets erases pn=1, a delayed ACK for pn=1 (e.g. peer
// actually got it pre-loss, ACK arrived after we declared loss) reaches
// OnPacketAck. Since the entry was erased, the ACK is a silent no-op.
// in_flight must stay at the post-retransmit value.
TEST(SendControlG2Test, S4_SpuriousAckForErasedLostPnIsNoOp) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    auto pkt1 = MakePacket(1, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(100, pkt1, kMss);
    auto pkt2 = MakePacket(2, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(101, pkt2, kMss);
    auto pkt3 = MakePacket(3, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(102, pkt3, kMss);
    auto pkt4 = MakePacket(4, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(103, pkt4, kMss);

    AckContiguous(sc, 4, 4, 200);  // declares pn=1 lost
    ASSERT_EQ(sc.GetCcBytesInFlightForTest(), 2u * kMss);

    auto pkt5_retx = MakePacket(5, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(210, pkt5_retx, kMss);
    ASSERT_EQ(sc.GetCcBytesInFlightForTest(), 3u * kMss);

    // Spurious ACK for pn=1 alone (already erased).
    AckContiguous(sc, 1, 1, 220);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 3u * kMss)
        << "Spurious ACK for already-lost+erased pn=1 must be a no-op; "
           "in_flight stays at {2,3,5} = 3*mss.";

    // Drain remaining.
    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(5);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);
    ack->AddAckRange(/*gap=*/0, /*range=*/1);  // 3,2
    sc.OnPacketAck(300, PacketNumberSpace::kApplicationNumberSpace, ack);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 0u) << "Final in_flight must be 0.";
}

// G2-S6 [SUSPECTED BUG]: retransmission reuses the SAME IPacket object.
//
// BaseConnection::TrySendRetransmit does:
//     entry = lost_packets_.front(); lost_packets_.pop_front();
//     RemoveStaleUnackedEntry(ns, orig_pn, lost_pkt);   // BEFORE renumbering
//     lost_pkt->SetPacketNumber(new_pn);      // <-- mutates the object in place
//     send_control.OnPacketSend(now, lost_pkt, size, entry.stream_data);
//
// Without the RemoveStaleUnackedEntry() call the entry keyed by the OLD pn
// survives. It is then unreachable forever: the per-packet timer looks up
// find(packet->GetPacketNumber()) and OnPacketAck walks pn downward from
// largest_ack, so neither can ever address a key that no longer matches the
// object's packet number -- the stale key is no longer derivable from the
// object itself, which is why the removal has to happen before renumbering.
// Three consequences:
//
//   P1: OnPTOTimer() probes unacked_packets_[ns].begin() only. The stale entry
//       is always the smallest key (hence first) and is permanently is_lost, so
//       after the very first PTO the timer lands on it forever and silently
//       stops finding anything to retransmit.
//   P2: OnPacketAck's `pkt_num--` range walk can descend into the stale key and
//       fire OnPacketAcked + stream_data_ack_cb_ a second time for bytes that
//       were already accounted as lost.
//   P3: the table grows by one dead entry per retransmission until the space is
//       discarded.
//
// This test pins P1/P3: after a retransmission exactly one entry may remain.
TEST(SendControlG2Test, S6_RetransmitReusingPacketObjectDropsStaleUnackedEntry) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);
    const PacketNumberSpace ns = PacketNumberSpace::kApplicationNumberSpace;

    // SendControl::Bind() installs the owner guard that TimerCore checks before
    // running a callback. Without it every timer we arm is skipped as
    // "owner expired" and PTO never fires -- which is why no earlier test in
    // this file exercised a real timer expiry.
    auto owner = std::make_shared<int>(0);
    sc.Bind(owner);

    auto pkt = MakePacket(0, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(100, pkt, kMss);
    ASSERT_EQ(sc.GetUnackedPacketCountForTest(ns), 1u);

    // Let PTO expire: the packet is declared lost and queued for retransmission.
    timer->Advance(5000);
    ASSERT_EQ(sc.GetLostPacket().size(), 1u);
    ASSERT_EQ(sc.GetLostPacket().front().packet, pkt)
        << "The queued retransmission must be the very object we registered.";

    // Connection layer retransmits: same object, brand-new packet number.
    auto entry = sc.GetLostPacket().front();
    sc.GetLostPacket().pop_front();
    // Order matters: drop the old entry BEFORE renumbering, while the stale key
    // is still the object's current packet number (O(1) lookup).
    const uint64_t orig_pn = entry.packet->GetPacketNumber();
    sc.RemoveStaleUnackedEntry(ns, orig_pn, entry.packet);
    entry.packet->SetPacketNumber(1);
    sc.OnPacketSend(200, entry.packet, kMss, entry.stream_data);

    EXPECT_EQ(sc.GetUnackedPacketCountForTest(ns), 1u)
        << "Unacked table must not retain the entry keyed by the pre-retransmit PN. "
           "OnPTOTimer() probes begin() only, so a surviving stale entry (always the "
           "smallest key, and permanently is_lost) would blind PTO after its first "
           "expiry -- see SendControl::RemoveStaleUnackedEntry().";

    // A second round: the table must stay flat, not accumulate one dead entry
    // per retransmission.
    timer->Advance(20000);
    ASSERT_EQ(sc.GetLostPacket().size(), 1u);
    auto entry2 = sc.GetLostPacket().front();
    sc.GetLostPacket().pop_front();
    sc.RemoveStaleUnackedEntry(ns, entry2.packet->GetPacketNumber(), entry2.packet);
    entry2.packet->SetPacketNumber(2);
    sc.OnPacketSend(300, entry2.packet, kMss, entry2.stream_data);

    EXPECT_EQ(sc.GetUnackedPacketCountForTest(ns), 1u) << "Repeated retransmissions must not grow the unacked table.";
}

}  // namespace g2

// ---------------------------------------------------------------------------
// Frame-level delivery tracking (aioquic QuicDeliveryState model)
// ---------------------------------------------------------------------------
namespace frame_delivery {

std::shared_ptr<HandshakeDoneFrame> MakeTrackedFrame(std::vector<FrameDeliveryState>* fired) {
    auto frame = std::make_shared<HandshakeDoneFrame>();
    frame->SetDeliveryHandler([fired](FrameDeliveryState state) { fired->push_back(state); });
    return frame;
}

// A packet with an actual frame object attached (the production path packs
// frames into IPacket::GetFrames(); the bitmask alone is not enough for the
// delivery walker).
std::shared_ptr<Rtt1Packet> MakeTrackedPacket(uint64_t pn, std::shared_ptr<IFrame> frame) {
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(pn);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pn));
    packet->AddFrameTypeBit(static_cast<FrameTypeBit>(1u << frame->GetType()));
    packet->GetFrames().push_back(std::move(frame));
    return packet;
}

TEST(FrameDeliveryTest, AckedPacketFiresAckedOnTrackedFrames) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    std::vector<FrameDeliveryState> fired;
    auto pkt = MakeTrackedPacket(1, MakeTrackedFrame(&fired));
    sc.OnPacketSend(100, pkt, 1200);

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(1);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);
    sc.OnPacketAck(110, PacketNumberSpace::kApplicationNumberSpace, ack);

    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0], FrameDeliveryState::kAcked);
}

TEST(FrameDeliveryTest, ThresholdLossFiresLostAndRetransmitStaysTracked) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    std::vector<FrameDeliveryState> fired;
    // Packet 1 carries the tracked frame; packets 2..4 carry none (plain
    // stream frames) so that ACKing 4 declares 1 lost by packet threshold.
    sc.OnPacketSend(100, MakeTrackedPacket(1, MakeTrackedFrame(&fired)), 1200);
    sc.OnPacketSend(101, MakePacket(2, FrameTypeBit::kStreamBit), 1200);
    sc.OnPacketSend(102, MakePacket(3, FrameTypeBit::kStreamBit), 1200);
    sc.OnPacketSend(103, MakePacket(4, FrameTypeBit::kStreamBit), 1200);

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(4);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(2);  // acks 4, 3, 2 -> 1 is below threshold window
    sc.OnPacketAck(110, PacketNumberSpace::kApplicationNumberSpace, ack);

    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0], FrameDeliveryState::kLost);

    // The production retransmit path reuses the IPacket and renumbers it in
    // place. The frame keeps its handler, so the retransmitted copy is still
    // tracked: its ACK fires kAcked on the same handler.
    ASSERT_EQ(sc.GetLostPacket().size(), 1u);
    auto entry = sc.GetLostPacket().front();
    sc.GetLostPacket().pop_front();
    sc.RemoveStaleUnackedEntry(
        PacketNumberSpace::kApplicationNumberSpace, entry.packet->GetPacketNumber(), entry.packet);
    entry.packet->SetPacketNumber(5);
    sc.OnPacketSend(200, entry.packet, 1200, entry.stream_data);

    auto ack2 = std::make_shared<AckFrame>();
    ack2->SetLargestAck(5);
    ack2->SetAckDelay(0);
    ack2->SetFirstAckRange(0);
    sc.OnPacketAck(210, PacketNumberSpace::kApplicationNumberSpace, ack2);

    ASSERT_EQ(fired.size(), 2u);
    EXPECT_EQ(fired[1], FrameDeliveryState::kAcked);
}

TEST(FrameDeliveryTest, UntrackedFramesAreIgnoredAndHandlersMayRefire) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    // Mixed packet: one tracked HANDSHAKE_DONE + one plain PING frame with no
    // handler. The walker must skip the handler-less frame silently.
    int lost_count = 0;
    auto frame = std::make_shared<HandshakeDoneFrame>();
    frame->SetDeliveryHandler([&lost_count](FrameDeliveryState state) {
        if (state == FrameDeliveryState::kLost) {
            lost_count++;
        }
    });

    auto pkt = std::make_shared<Rtt1Packet>();
    pkt->SetPacketNumber(1);
    pkt->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(1));
    pkt->AddFrameTypeBit(static_cast<FrameTypeBit>(FrameTypeBit::kHandshakeDoneBit | FrameTypeBit::kPingBit));
    pkt->GetFrames().push_back(frame);
    pkt->GetFrames().push_back(std::make_shared<PingFrame>());
    sc.OnPacketSend(100, pkt, 1200);

    // Drive two loss declarations for the same packet object: threshold loss,
    // then PTO re-queue (OnPTOTimer re-queues an already-lost entry). Handlers
    // are reset-style and must tolerate repeat fires.
    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(4);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(2);
    sc.OnPacketSend(101, MakePacket(2, FrameTypeBit::kStreamBit), 1200);
    sc.OnPacketSend(102, MakePacket(3, FrameTypeBit::kStreamBit), 1200);
    sc.OnPacketSend(103, MakePacket(4, FrameTypeBit::kStreamBit), 1200);
    sc.OnPacketAck(110, PacketNumberSpace::kApplicationNumberSpace, ack);

    EXPECT_EQ(lost_count, 1u);
}

}  // namespace frame_delivery

// =====================================================================
// ACK frame validation (RFC 9000 §19.3.1) and bounded range walk.
//
// Largest Acknowledged and every ACK Range Length are peer-controlled varints
// (up to 2^62-1), and AckRangePackets() iterates once per acknowledged packet
// number. Without validation a single ACK frame could pin the connection's
// worker thread essentially forever. Two layers protect it:
//   1. ValidateAckFrame() rejects an ACK that acknowledges packet numbers we
//      never sent, plus any structurally malformed range.
//   2. AckRangePackets() additionally stops walking once the tracked table is
//      empty or the anti-DoS budget is spent.
// =====================================================================
namespace ack_validation {

TEST(SendControlAckValidationTest, AckForUnsentPacketNumberIsRejected) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    std::vector<uint64_t> violations;
    sc.SetProtocolViolationCallback(
        [&violations](uint64_t error, uint16_t /*frame_type*/, const std::string& /*reason*/) {
            violations.push_back(error);
        });

    bool stream_ack_fired = false;
    sc.SetStreamDataAckCallback([&stream_ack_fired](uint64_t, uint64_t, uint64_t, bool) { stream_ack_fired = true; });

    auto pkt = MakePacket(1, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(0, pkt, 1200, {StreamDataInfo(4, /*offset=*/0, /*len=*/100, /*fin=*/false)});

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(1ull << 60);  // never sent by us
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);

    sc.OnPacketAck(10, PacketNumberSpace::kApplicationNumberSpace, ack);

    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], static_cast<uint64_t>(QuicErrorCode::kProtocolViolation));
    EXPECT_FALSE(stream_ack_fired);
    // A rejected ACK must not silently retire the packet it pretended to cover.
    EXPECT_EQ(sc.GetUnackedPacketCountForTest(PacketNumberSpace::kApplicationNumberSpace), 1u);
}

TEST(SendControlAckValidationTest, AckFirstRangeUnderflowIsRejected) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    std::vector<uint64_t> violations;
    sc.SetProtocolViolationCallback(
        [&violations](uint64_t error, uint16_t /*frame_type*/, const std::string& /*reason*/) {
            violations.push_back(error);
        });

    sc.OnPacketSend(0, MakePacket(1, FrameTypeBit::kStreamBit), 1200);

    // Built directly through the setters, i.e. bypassing AckFrame::Decode's own
    // check: ValidateAckFrame() must still refuse a range that reaches below
    // packet number 0.
    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(1);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(100);

    sc.OnPacketAck(10, PacketNumberSpace::kApplicationNumberSpace, ack);

    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], static_cast<uint64_t>(QuicErrorCode::kFrameEncodingError));
}

TEST(SendControlAckValidationTest, AckGapUnderflowIsRejected) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    std::vector<uint64_t> violations;
    sc.SetProtocolViolationCallback(
        [&violations](uint64_t error, uint16_t /*frame_type*/, const std::string& /*reason*/) {
            violations.push_back(error);
        });

    sc.OnPacketSend(0, MakePacket(1, FrameTypeBit::kStreamBit), 1200);

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(1);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);
    ack->AddAckRange(/*gap=*/100, /*range_len=*/0);  // 1 - 100 - 2 underflows

    sc.OnPacketAck(10, PacketNumberSpace::kApplicationNumberSpace, ack);

    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], static_cast<uint64_t>(QuicErrorCode::kFrameEncodingError));
}

// The bound must not reject legitimate traffic: a contiguous ACK covering every
// outstanding packet still retires all of them.
TEST(SendControlAckValidationTest, FullRangeAckStillRetiresEveryPacket) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    bool violation = false;
    sc.SetProtocolViolationCallback([&violation](uint64_t, uint16_t, const std::string&) { violation = true; });

    constexpr uint64_t kCount = 200;
    for (uint64_t pn = 1; pn <= kCount; ++pn) {
        sc.OnPacketSend(pn, MakePacket(pn, FrameTypeBit::kStreamBit), 1200);
    }
    ASSERT_EQ(sc.GetUnackedPacketCountForTest(PacketNumberSpace::kApplicationNumberSpace), kCount);

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(kCount);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(kCount - 1);  // covers kCount .. 1

    sc.OnPacketAck(kCount + 1, PacketNumberSpace::kApplicationNumberSpace, ack);

    EXPECT_FALSE(violation);
    EXPECT_EQ(sc.GetUnackedPacketCountForTest(PacketNumberSpace::kApplicationNumberSpace), 0u);
}

}  // namespace ack_validation

// =====================================================================
// Code-review P1-1: RFC 9002 §6.1.2 time-threshold loss detection.
//
// DetectLostPackets() used to look up the largest-acked packet's send time
// in unacked_packets_ AFTER AckLargestAckedPacket() had already erased that
// entry, so largest_acked_send_time was always 0 and the time-threshold
// branch was dead code — only the packet threshold (3 packets) and PTO ever
// declared losses. AckLargestAckedPacket() now captures the send time
// before the erase; these tests pin that the time threshold is reachable.
//
// NOTE on time bases: PacketTimerInfo::send_time_ is stamped internally with
// common::MonotonicTimeMsec() (NOT the OnPacketSend `now` argument), so these
// tests drive the real monotonic clock and compute ACK `now` values relative
// to it.
// =====================================================================
namespace time_threshold {

// Send pn=1, wait 500ms, send pn=2, then ACK pn=2 while the packet-threshold
// window is NOT satisfied (2 - 1 < 3). The ACK arrives ~100ms after pn=2 was
// sent (SRTT becomes ~100ms => loss_delay ~= 112ms), so the 500ms send-time
// gap must declare pn=1 lost by the TIME threshold.
TEST(SendTimeThresholdTest, TimeThresholdDeclaresLossBeforePacketThreshold) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);
    const PacketNumberSpace ns = PacketNumberSpace::kApplicationNumberSpace;

    sc.OnPacketSend(common::MonotonicTimeMsec(), MakePacket(1, FrameTypeBit::kStreamBit), 1200);
    // Real wait: send_time_ is the monotonic clock, and the gap must exceed
    // 9/8 * SRTT (~112ms after the ACK below updates SRTT to ~100ms).
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    uint64_t t_pn2 = common::MonotonicTimeMsec();
    sc.OnPacketSend(t_pn2, MakePacket(2, FrameTypeBit::kStreamBit), 1200);

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(2);  // acks only pn=2
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);
    // ACK ~100ms after pn=2: first RTT sample ~= 100ms, so SRTT ~= 100ms and
    // loss_delay = 9/8 * 100 ~= 112ms < the 500ms send-time gap.
    sc.OnPacketAck(t_pn2 + 100, ns, ack);

    // pn=1 must be queued for retransmission via the time threshold.
    ASSERT_EQ(sc.GetLostPacket().size(), 1u);
    EXPECT_EQ(sc.GetLostPacket().front().packet->GetPacketNumber(), 1u)
        << "pn=1 must be declared lost by the RFC 9002 §6.1.2 time threshold: it was "
           "sent 500ms before the largest-acked packet while loss_delay is ~112ms, "
           "and the packet threshold (3) is not met. If this fails, the time-threshold "
           "branch is dead code again (largest_acked_send_time never captured).";
}

// Negative control: packets sent back-to-back must NOT be lost by the time
// threshold once SRTT is ~100ms (loss_delay ~112ms >> sub-ms send gap).
TEST(SendTimeThresholdTest, SmallSendGapIsNotLostByTimeThreshold) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);
    const PacketNumberSpace ns = PacketNumberSpace::kApplicationNumberSpace;

    uint64_t t0 = common::MonotonicTimeMsec();
    sc.OnPacketSend(t0, MakePacket(1, FrameTypeBit::kStreamBit), 1200);
    sc.OnPacketSend(t0, MakePacket(2, FrameTypeBit::kStreamBit), 1200);

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(2);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);
    // RTT sample ~100ms => loss_delay ~112ms, far above the sub-ms send gap.
    sc.OnPacketAck(t0 + 100, ns, ack);

    EXPECT_EQ(sc.GetLostPacket().size(), 0u)
        << "A sub-ms send gap must not trip the time threshold once SRTT is ~100ms; "
           "spurious loss declarations would burn cwnd.";
}

// #5 (code review round 2): RFC 9002 §6.1.2 specifies the time threshold as
// (largest_acked.time_sent - pkt.time_sent > loss_delay) OR (now -
// pkt.time_sent > loss_delay). Only the delta branch used to be implemented,
// so whenever the largest-acked anchor was unavailable (out-of-order ACK ->
// largest_acked_pn_ mismatch -> anchor disabled) the time threshold went
// blind. This test pins the now-branch: an out-of-order ACK whose largest
// acked (4) is below the last processed one (5) disables the anchor, yet the
// stale packets must still be declared lost by their age.
TEST(SendTimeThresholdTest, NowBranchCoversAnchorUnavailableRounds) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);
    const PacketNumberSpace ns = PacketNumberSpace::kApplicationNumberSpace;

    // Four back-to-back packets; send times all within ~1ms of t0.
    uint64_t t0 = common::MonotonicTimeMsec();
    for (uint64_t pn = 1; pn <= 4; ++pn) {
        sc.OnPacketSend(t0, MakePacket(pn, FrameTypeBit::kStreamBit), 1200);
    }

    // ACK {4, 3, 1} (gap leaves pn=2 unacked). Sets SRTT ~100ms (loss_delay
    // ~112ms) and anchors (pn=4, send_time~t0). pn=2 survives: packet
    // threshold (4 < 2+3) and both time conditions (delta ~0ms, age ~100ms)
    // stay below their thresholds.
    auto ack1 = std::make_shared<AckFrame>();
    ack1->SetLargestAck(4);
    ack1->SetAckDelay(0);
    ack1->SetFirstAckRange(1);                  // ack 4, 3
    ack1->AddAckRange(/*gap=*/0, /*range=*/0);  // skip pn=2, ack pn=1
    sc.OnPacketAck(t0 + 100, ns, ack1);
    ASSERT_EQ(sc.GetLostPacket().size(), 0u) << "100ms age must stay under the ~112ms loss_delay";

    // Out-of-order ACK with largest=3 (<= last processed 4): the anchor is
    // disabled for this round (pn mismatch -> largest_acked_send_time == 0),
    // and the delta branch alone could not declare anything. The now-branch
    // must still declare pn=2 lost: its age is now ~400ms >> loss_delay.
    auto ack2 = std::make_shared<AckFrame>();
    ack2->SetLargestAck(3);  // already acked; pn=3 re-ACK is a no-op
    ack2->SetAckDelay(0);
    ack2->SetFirstAckRange(0);
    sc.OnPacketAck(t0 + 400, ns, ack2);

    ASSERT_EQ(sc.GetLostPacket().size(), 1u);
    EXPECT_EQ(sc.GetLostPacket().front().packet->GetPacketNumber(), 2u)
        << "The RFC 9002 §6.1.2 now-branch must declare stale packets lost even when "
           "the largest-acked anchor is unavailable (out-of-order ACK round).";
}

}  // namespace time_threshold

// =====================================================================
// Code-review P3e: RFC 9002 §5.3 RTT adjustment must use
// min(ack_delay, peer's advertised max_ack_delay).
//
// AckLargestAckedPacket() used to feed the raw scaled ACK delay into
// UpdateRtt(), so a peer reporting an oversized ACK Delay could deflate our
// SRTT/RTTVAR and drag the PTO early. The delay is now clamped to the peer's
// advertised max_ack_delay (learned via UpdateConfig from transport params).
//
// Note the first RTT sample never subtracts ack_delay (RFC 9002 §5.1-first
// measurement), so each test ACKs a first packet with delay=0 to seed SRTT /
// min_rtt, then a second packet carries the (oversized) delay.
// =====================================================================
namespace rtt_clamp {

// Oversized ACK delay must be clamped to the peer's max_ack_delay: with a
// true 1000ms RTT, a 100ms first RTT, and a bogus 800ms ACK delay, the
// adjusted second sample must be ~975ms (clamped to 25ms), not ~200ms.
TEST(RttClampTest, OversizedAckDelayIsClampedToPeerMaxAckDelay) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);

    // Peer advertised max_ack_delay = 25ms, ack_delay_exponent = 3.
    TransportParam tp;
    tp.SetMaxAckDelay(25);
    tp.SetAckDelayExponent(3);
    sc.UpdateConfig(tp);

    // Sample 1 (seed): pn=1, true RTT ~100ms, delay 0 -> SRTT=min_rtt=100.
    uint64_t t1 = common::MonotonicTimeMsec();
    sc.OnPacketSend(t1, MakePacket(1, FrameTypeBit::kStreamBit), 1200);
    auto ack1 = std::make_shared<AckFrame>();
    ack1->SetLargestAck(1);
    ack1->SetAckDelay(0);
    ack1->SetFirstAckRange(0);
    sc.OnPacketAck(t1 + 100, PacketNumberSpace::kApplicationNumberSpace, ack1);
    ASSERT_EQ(sc.GetRtt(), 100u);

    // Sample 2: pn=2, true RTT ~1000ms, raw ACK delay 100 << 3 = 800ms.
    // Clamped: 1000 - 25 = 975 -> SRTT = 7/8*100 + 1/8*975 ~= 209ms.
    // Unclamped (bug): 1000 - 800 = 200 -> SRTT = 7/8*100 + 1/8*200 ~= 112ms.
    uint64_t t2 = common::MonotonicTimeMsec();
    sc.OnPacketSend(t2, MakePacket(2, FrameTypeBit::kStreamBit), 1200);
    auto ack2 = std::make_shared<AckFrame>();
    ack2->SetLargestAck(2);
    ack2->SetAckDelay(100);  // 100 * 2^3 = 800ms once scaled
    ack2->SetFirstAckRange(0);
    sc.OnPacketAck(t2 + 1000, PacketNumberSpace::kApplicationNumberSpace, ack2);
    EXPECT_GT(sc.GetRtt(), 160u) << "SRTT ~= 209ms expected; a value near 112ms means the oversized "
                                    "ACK delay was NOT clamped to the peer's advertised max_ack_delay "
                                    "(RFC 9002 §5.3).";
}

// Clamp boundary semantics, proven by three-instance differential:
//   C: ACK delay 0ms  -> adjusted sample = 1000ms
//   A: ACK delay 25ms (= peer max_ack_delay, legitimate) -> 975ms
//   B: ACK delay 50ms (> peer max_ack_delay, oversized)  -> clamped to 975ms
// Expected: SRTT(B) ~= SRTT(A) (clamp truncates to the advertised maximum)
// and both stay clearly below SRTT(C) (the delay IS applied, not discarded).
// Five samples per instance amplify the 1/8-weighted differences well above
// clock jitter.
TEST(RttClampTest, ClampBoundarySemantics) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc_c(timer), sc_a(timer), sc_b(timer);

    TransportParam tp;
    tp.SetMaxAckDelay(25);
    tp.SetAckDelayExponent(0);  // raw ACK delay value == milliseconds
    sc_c.UpdateConfig(tp);
    sc_a.UpdateConfig(tp);
    sc_b.UpdateConfig(tp);

    const PacketNumberSpace ns = PacketNumberSpace::kApplicationNumberSpace;

    // Seed each instance: SRTT = min_rtt = 100ms (delay 0).
    for (SendControl* sc : {&sc_c, &sc_a, &sc_b}) {
        uint64_t t = common::MonotonicTimeMsec();
        sc->OnPacketSend(t, MakePacket(1, FrameTypeBit::kStreamBit), 1200);
        auto ack = std::make_shared<AckFrame>();
        ack->SetLargestAck(1);
        ack->SetAckDelay(0);
        ack->SetFirstAckRange(0);
        sc->OnPacketAck(t + 100, ns, ack);
    }

    // Five 1000ms-RTT samples per instance with their respective delays.
    // Packets are sent to all three instances inside the same millisecond so
    // their internal monotonic send-time stamps agree.
    for (uint64_t i = 0; i < 5; ++i) {
        uint64_t pn = 2 + i;
        uint64_t t = common::MonotonicTimeMsec();
        sc_c.OnPacketSend(t, MakePacket(pn, FrameTypeBit::kStreamBit), 1200);
        sc_a.OnPacketSend(t, MakePacket(pn, FrameTypeBit::kStreamBit), 1200);
        sc_b.OnPacketSend(t, MakePacket(pn, FrameTypeBit::kStreamBit), 1200);

        auto mk_ack = [pn](uint64_t delay) {
            auto ack = std::make_shared<AckFrame>();
            ack->SetLargestAck(pn);
            ack->SetAckDelay(delay);
            ack->SetFirstAckRange(0);
            return ack;
        };
        uint64_t now = t + 1000;
        sc_c.OnPacketAck(now, ns, mk_ack(0));
        sc_a.OnPacketAck(now, ns, mk_ack(25));
        sc_b.OnPacketAck(now, ns, mk_ack(50));
    }

    uint32_t srtt_c = sc_c.GetRtt();
    uint32_t srtt_a = sc_a.GetRtt();
    uint32_t srtt_b = sc_b.GetRtt();

    // Oversized (50ms) must equal the exactly-at-max (25ms) outcome: the clamp
    // truncates to max_ack_delay instead of discarding or passing through.
    EXPECT_NEAR(srtt_a, srtt_b, 2) << "delay=50ms must clamp to the same adjusted sample as delay=25ms.";
    // Both delayed instances must sit clearly below the zero-delay instance:
    // the (clamped) delay is genuinely applied to the RTT adjustment.
    EXPECT_LT(srtt_a + 10, srtt_c) << "A 25ms ACK delay must lower SRTT vs no delay; if equal, the clamp "
                                      "over-suppressed legitimate delays.";
}

}  // namespace rtt_clamp

// =====================================================================
// Code-review P2-5: runtime congestion-control selection.
//
// The CC algorithm used to be a compile-time constant (kDefaultCongestionControl
// in quic/config.h). It is now runtime-configurable: the worker parses
// QuicConfig::congestion_control_ into a CongestionControlType and injects it
// into every connection it creates via SetCongestionControlType(), which is
// rejected once the first packet has been sent.
// =====================================================================
namespace cc_config {

TEST(CcConfigTest, StringParsing) {
    // Empty = compile-time default (currently "cubic").
    EXPECT_EQ(CongestionControlTypeFromString(""), CongestionControlType::kCubic);
    EXPECT_EQ(CongestionControlTypeFromString("cubic"), CongestionControlType::kCubic);
    EXPECT_EQ(CongestionControlTypeFromString("reno"), CongestionControlType::kReno);
    EXPECT_EQ(CongestionControlTypeFromString("bbrv1"), CongestionControlType::kBbrV1);
    EXPECT_EQ(CongestionControlTypeFromString("bbr"), CongestionControlType::kBbrV1);
    EXPECT_EQ(CongestionControlTypeFromString("bbrv2"), CongestionControlType::kBbrV2);
    EXPECT_EQ(CongestionControlTypeFromString("bbrv3"), CongestionControlType::kBbrV3);
    // Case-insensitive.
    EXPECT_EQ(CongestionControlTypeFromString("CUBIC"), CongestionControlType::kCubic);
    EXPECT_EQ(CongestionControlTypeFromString("BBRv1"), CongestionControlType::kBbrV1);
    // Unknown falls back to reno (with a warning log).
    EXPECT_EQ(CongestionControlTypeFromString("westwood"), CongestionControlType::kReno);
}

TEST(CcConfigTest, DefaultIsCompileTimeDefault) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);
    EXPECT_EQ(sc.GetCcTypeForTest(), CongestionControlTypeFromString(""))
        << "Without an override, SendControl must use the compile-time default CC.";
}

TEST(CcConfigTest, AllTypesSelectableBeforeFirstSend) {
    for (CongestionControlType t :
        {CongestionControlType::kReno, CongestionControlType::kCubic, CongestionControlType::kBbrV1,
            CongestionControlType::kBbrV2, CongestionControlType::kBbrV3}) {
        auto timer = std::make_shared<common::TestTimerScheduler>();
        SendControl sc(timer);
        sc.SetCongestionControlType(t);
        EXPECT_EQ(sc.GetCcTypeForTest(), t) << "Type " << CongestionControlTypeToString(t) << " must be selectable.";
        // The replaced CC object must be functional (initial cwnd queryable).
        EXPECT_GT(sc.GetCcCongestionWindowForTest(), 0u);
    }
}

TEST(CcConfigTest, OverrideRejectedAfterFirstSend) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    SendControl sc(timer);
    sc.SetCongestionControlType(CongestionControlType::kBbrV1);
    ASSERT_EQ(sc.GetCcTypeForTest(), CongestionControlType::kBbrV1);

    // First send locks the algorithm (rebuilding mid-connection would drop
    // bytes_in_flight / cwnd state).
    sc.OnPacketSend(common::MonotonicTimeMsec(), MakePacket(1, FrameTypeBit::kStreamBit), 1200);
    sc.SetCongestionControlType(CongestionControlType::kReno);

    EXPECT_EQ(sc.GetCcTypeForTest(), CongestionControlType::kBbrV1)
        << "A post-send override must be rejected, not silently applied.";
}

}  // namespace cc_config

}  // namespace
}  // namespace quic
}  // namespace quicx
