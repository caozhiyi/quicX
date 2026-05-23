#include <gtest/gtest.h>
#include <memory>
#include <tuple>
#include <vector>

#include <quicx/common/if_event_loop.h>
#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"
#include "quic/connection/controler/send_control.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/type.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {
namespace {

class MockTimer: public common::ITimer {
public:
    uint32_t add_count = 0;
    uint32_t rm_count = 0;

    uint64_t AddTimer(common::TimerTask& task, uint32_t /*time*/, uint64_t /*now*/ = 0) override {
        add_count++;
        // Set task ID for test
        task.SetIdForTest(add_count);
        tasks_.push_back(task);
        return add_count;
    }

    bool RemoveTimer(common::TimerTask& task) override {
        // Find task by ID
        uint64_t id = task.GetId();
        for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
            if (it->GetId() == id) {
                tasks_.erase(it);
                rm_count++;
                return true;
            }
        }
        return false;
    }

    int32_t MinTime(uint64_t /*now*/ = 0) override { return tasks_.empty() ? -1 : 0; }
    void TimerRun(uint64_t /*now*/ = 0) override {}
    bool Empty() override { return tasks_.empty(); }

private:
    std::vector<common::TimerTask> tasks_;
};

std::shared_ptr<Rtt1Packet> MakePacket(uint64_t packet_number, FrameTypeBit frame_bits) {
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(packet_number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(packet_number));
    packet->AddFrameTypeBit(frame_bits);
    return packet;
}

TEST(SendControlTest, AckElicitingPacketsTriggerCallbacks) {
    auto timer = std::make_shared<MockTimer>();
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    event_loop->SetTimerForTest(timer);
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

    // Expect 4 timer adds:
    // - 2 for packet timeout timers (one per packet)
    // - 2 for PTO timer (scheduled after each packet send, with the second one replacing the first)
    EXPECT_EQ(timer->add_count, 4u);

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

    // Expect 4 timer removes:
    // - 1 for removing old PTO timer when sending packet 10
    // - 2 for packet timeout timers (cancelled when ACKed)
    // - 1 for PTO timer (cancelled when ACK received)
    EXPECT_EQ(timer->rm_count, 4u);
}

TEST(SendControlTest, NonAckElicitingPacketsAreNotTracked) {
    auto timer = std::make_shared<MockTimer>();
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    event_loop->SetTimerForTest(timer);
    SendControl send_control(timer);

    bool callback_invoked = false;
    send_control.SetStreamDataAckCallback(
        [&callback_invoked](uint64_t /*stream_id*/, uint64_t /*offset_start*/, uint64_t /*length*/, bool /*has_fin*/) {
            callback_invoked = true;
        });

    // Padding-only packet should not be considered ack-eliciting
    auto packet = MakePacket(1, FrameTypeBit::kPaddingBit);
    std::vector<StreamDataInfo> stream_info = {StreamDataInfo(8, /*offset=*/0, /*len=*/42, /*fin=*/false)};
    send_control.OnPacketSend(0, packet, 1000, stream_info);

    EXPECT_EQ(timer->add_count, 0u);  // Timer not armed

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(1);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);

    send_control.OnPacketAck(5, PacketNumberSpace::kApplicationNumberSpace, ack);
    EXPECT_FALSE(callback_invoked);
    EXPECT_EQ(timer->rm_count, 0u);
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
    auto timer = std::make_shared<MockTimer>();
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    event_loop->SetTimerForTest(timer);
    SendControl sc(timer);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 0u);

    constexpr uint64_t kPktCount = 5;
    for (uint64_t pn = 1; pn <= kPktCount; ++pn) {
        auto pkt = MakePacket(pn, FrameTypeBit::kStreamBit);
        sc.OnPacketSend(/*now*/ 100 + pn, pkt, kMss);
    }

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), kPktCount * kMss)
        << "After sending " << kPktCount
        << " ack-eliciting packets, in_flight must be exactly N*mss.";

    AckContiguous(sc, /*low*/ 1, /*high*/ kPktCount, /*now*/ 300);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 0u)
        << "After ACKing every sent packet, in_flight must drain to 0.";
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
    auto timer = std::make_shared<MockTimer>();
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    event_loop->SetTimerForTest(timer);
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
    ack->SetFirstAckRange(0);                   // pn=3
    ack->AddAckRange(/*gap=*/0, /*range=*/0);   // RFC: pn=1
    sc.OnPacketAck(200, PacketNumberSpace::kApplicationNumberSpace, ack);

    // RFC-compliant outcome: pn=3 (offset 300) and pn=1 (offset 100) ACKed.
    ASSERT_EQ(acked_stream_offsets.size(), 2u);
    EXPECT_EQ(acked_stream_offsets[0], 300u) << "pn=3 ACKed first (largest_ack)";
    EXPECT_EQ(acked_stream_offsets[1], 100u)
        << "pn=1 must be ACKed by addrange{gap=0,len=0}. If got 200, the "
           "G2 off-by-one regression is back: send_control.cpp ~line 307 "
           "must subtract (gap+2), not (gap+1).";

    // pn=2 is in the gap and must remain in flight.
    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), kMss)
        << "Only pn=2 should remain in flight after the selective ACK.";
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
    auto timer = std::make_shared<MockTimer>();
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    event_loop->SetTimerForTest(timer);
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

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 2u * kMss)
        << "After ACK(4) + loss(1): outstanding {2,3} = 2*mss.";

    auto pkt5_retx = MakePacket(5, FrameTypeBit::kStreamBit);
    sc.OnPacketSend(220, pkt5_retx, kMss);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 3u * kMss)
        << "After retransmit pn=5: outstanding {2,3,5} = 3*mss.";

    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(5);
    ack->SetAckDelay(0);
    ack->SetFirstAckRange(0);   // pn=5
    ack->AddAckRange(/*gap=*/0, /*range=*/1);  // skip pn=4 (already ACKed), cover 3,2
    sc.OnPacketAck(300, PacketNumberSpace::kApplicationNumberSpace, ack);

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 0u)
        << "All packets accounted for; in_flight must be 0. "
           "If non-zero, send_control LEAKED in_flight by "
        << sc.GetCcBytesInFlightForTest()
        << " bytes via the DetectLost path -- this is the G2 fingerprint.";
}

// G2-S4 [SUSPECTED BUG]: Spurious ACK for already-erased pn.
//
// After DetectLostPackets erases pn=1, a delayed ACK for pn=1 (e.g. peer
// actually got it pre-loss, ACK arrived after we declared loss) reaches
// OnPacketAck. Since the entry was erased, the ACK is a silent no-op.
// in_flight must stay at the post-retransmit value.
TEST(SendControlG2Test, S4_SpuriousAckForErasedLostPnIsNoOp) {
    auto timer = std::make_shared<MockTimer>();
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    event_loop->SetTimerForTest(timer);
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

    EXPECT_EQ(sc.GetCcBytesInFlightForTest(), 0u)
        << "Final in_flight must be 0.";
}

}  // namespace g2

}  // namespace
}  // namespace quic
}  // namespace quicx
