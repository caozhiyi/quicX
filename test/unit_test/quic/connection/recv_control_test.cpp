#include <memory>

#include <gtest/gtest.h>

#include "common/network/if_event_loop.h"
#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"

#include "quic/connection/controller/recv_control.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/type.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/rtt_1_packet.h"

#include "test/unit_test/common/timer/test_timer_scheduler.h"

namespace quicx {
namespace quic {
namespace {

std::shared_ptr<Rtt1Packet> MakePacket(uint64_t number, FrameTypeBit frame_bits) {
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(number);
    packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(number));
    packet->AddFrameTypeBit(frame_bits);
    return packet;
}

TEST(RecvControlTest, AckFrameGeneratedForAckElicitingPackets) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    RecvControl recv_control(timer);

    recv_control.OnPacketRecv(100, MakePacket(5, FrameTypeBit::kStreamBit));
    recv_control.OnPacketRecv(101, MakePacket(4, FrameTypeBit::kStreamBit));
    recv_control.OnPacketRecv(150, MakePacket(2, FrameTypeBit::kStreamBit));

    EXPECT_EQ(timer->ArmCount(), 1u);  // Timer armed only once despite multiple packets

    auto frame = recv_control.MayGenerateAckFrame(160, PacketNumberSpace::kApplicationNumberSpace, false);
    ASSERT_NE(frame, nullptr);
    auto ack = std::dynamic_pointer_cast<AckFrame>(frame);
    ASSERT_NE(ack, nullptr);

    EXPECT_EQ(ack->GetLargestAck(), 5u);
    // ACK Delay is clamped to max_ack_delay_ (10ms by default) before the
    // exponent shift: raw delay was 60ms (160 - 100), clamped to 10ms,
    // 10 >> 3 = 1. The clamp is defence in depth against runaway delays
    // poisoning the peer's RTT estimator (kwik interop, 2026-09-16).
    EXPECT_EQ(ack->GetAckDelay(), 1u);
    EXPECT_EQ(ack->GetFirstAckRange(), 1u);  // packets 5 and 4 contiguous

    const auto& ranges = ack->GetAckRange();
    ASSERT_EQ(ranges.size(), 1u);
    // RFC 9000 §19.3.1: Gap is encoded as (actual unacked count) - 1.
    // Between run [4..4] and run [2..2] only packet 3 is unacked,
    // so actual_unacked_count = 1 and gap_value = 0.
    EXPECT_EQ(ranges[0].GetGap(), 0u);
    EXPECT_EQ(ranges[0].GetAckRangeLength(), 0u);  // single packet range (packet 2)

    EXPECT_EQ(timer->PendingCount(), 0u);  // Timer cancelled when ACK generated
}

// Regression (L1 handshakeloss 120s deadlock, 2026-09-16): the FIRST packet
// of a packet number space with pn == 0 never updated largest_recv_time_
// (the old strict `largest < pn` is false for 0 < 0), so the first ACK[0]
// reported ack_delay = now - 0 = the host's full steady_clock uptime
// (2.8e9 ms observed on the interop runner). The peer (kwik) overflowed its
// int math into a NEGATIVE delay, poisoned smoothed_rtt to ~1477 s, and its
// loss detection / PTO went silent for the rest of the connection.
TEST(RecvControlTest, FirstPacketPnZeroRecordsRecvTime) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    RecvControl recv_control(timer);

    // First packet of the Application space is pn 0 (e.g. the server's
    // HANDSHAKE_DONE packet in the reverse direction). Its receive time MUST
    // be recorded. Generate the ACK 7ms later: fixed code reports 7 >> 3 = 0;
    // the unfixed code would report (107 - 0) clamped to 10 >> 3 = 1.
    recv_control.OnPacketRecv(100, MakePacket(0, FrameTypeBit::kStreamBit));
    auto frame = recv_control.MayGenerateAckFrame(107, PacketNumberSpace::kApplicationNumberSpace, false);
    ASSERT_NE(frame, nullptr);
    auto ack = std::dynamic_pointer_cast<AckFrame>(frame);
    ASSERT_NE(ack, nullptr);
    EXPECT_EQ(ack->GetLargestAck(), 0u);
    EXPECT_EQ(ack->GetAckDelay(), 0u);  // (107 - 100) >> 3 = 0, NOT uptime-clamped
}

// Regression (same run): the ACK Delay field must never exceed the
// max_ack_delay we effectively honour, whatever the raw elapsed time is.
TEST(RecvControlTest, AckDelayClampedToMaxAckDelay) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    RecvControl recv_control(timer);

    recv_control.OnPacketRecv(100, MakePacket(3, FrameTypeBit::kStreamBit));
    // 3600 s later the raw delay is huge; it must be clamped to the default
    // max_ack_delay_ (10 ms) before encoding: 10 >> 3 = 1. An unclamped value
    // (3600000 >> 3) once overflowed the peer's RTT estimator.
    auto frame = recv_control.MayGenerateAckFrame(3600100, PacketNumberSpace::kApplicationNumberSpace, false);
    ASSERT_NE(frame, nullptr);
    auto ack = std::dynamic_pointer_cast<AckFrame>(frame);
    ASSERT_NE(ack, nullptr);
    EXPECT_EQ(ack->GetAckDelay(), 1u);
}

TEST(RecvControlTest, NonAckElicitingPacketsTrackedButNoImmediateAck) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    RecvControl recv_control(timer);

    // RFC 9000 §13.2: non-ack-eliciting packets must be tracked (so the next
    // outgoing ACK covers them) but must NOT trigger an immediate ACK.
    recv_control.OnPacketRecv(200, MakePacket(10, FrameTypeBit::kAckBit));
    EXPECT_EQ(timer->ArmCount(), 0u);

    // The packet IS tracked, so MayGenerateAckFrame produces an ACK covering it.
    auto frame = recv_control.MayGenerateAckFrame(205, PacketNumberSpace::kApplicationNumberSpace, false);
    EXPECT_NE(frame, nullptr);
    EXPECT_EQ(timer->PendingCount(), 0u);
}

TEST(RecvControlTest, SecondConsecutiveAckOnlyPacketTriggersAntiDeadlockAck) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    RecvControl recv_control(timer);

    // RFC 9000 §13.2.1 para 6: an endpoint SHOULD acknowledge at least every
    // second ACK-only packet. Peers whose PTO probes carry no data (aioquic)
    // depend on this ACK (and its ranges exposing the gap at their lost
    // request) to declare the request lost and retransmit it.
    uint32_t active_send_calls = 0;
    recv_control.SetActiveSendCB([&active_send_calls]() { active_send_calls++; });

    // First ACK-only packet: tracked, no ACK triggered.
    recv_control.OnPacketRecv(400, MakePacket(10, FrameTypeBit::kAckBit));
    EXPECT_EQ(active_send_calls, 0u);
    EXPECT_FALSE(recv_control.ShouldSendAckNow(PacketNumberSpace::kApplicationNumberSpace));

    // Second consecutive ACK-only packet: anti-deadlock ACK becomes due and
    // the send loop is kicked exactly once.
    recv_control.OnPacketRecv(401, MakePacket(11, FrameTypeBit::kAckBit));
    EXPECT_EQ(active_send_calls, 1u);
    EXPECT_TRUE(recv_control.ShouldSendAckNow(PacketNumberSpace::kApplicationNumberSpace));

    // The generated ACK covers both packets.
    auto frame = recv_control.MayGenerateAckFrame(405, PacketNumberSpace::kApplicationNumberSpace, false);
    ASSERT_NE(frame, nullptr);
    auto ack = std::dynamic_pointer_cast<AckFrame>(frame);
    ASSERT_NE(ack, nullptr);
    EXPECT_EQ(ack->GetLargestAck(), 11u);
    EXPECT_FALSE(recv_control.ShouldSendAckNow(PacketNumberSpace::kApplicationNumberSpace));
}

TEST(RecvControlTest, AckElicitingPacketResetsAckOnlySeqCounter) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    RecvControl recv_control(timer);

    uint32_t active_send_calls = 0;
    recv_control.SetActiveSendCB([&active_send_calls]() { active_send_calls++; });

    // One ACK-only packet, then an ack-eliciting packet (resets the
    // consecutive counter), then a single ACK-only packet: the sequence
    // never reaches two consecutive ACK-only packets, so no anti-deadlock
    // ACK is triggered by the ACK-only path. The stream packet itself goes
    // through the normal delayed/threshold path instead.
    recv_control.OnPacketRecv(500, MakePacket(20, FrameTypeBit::kAckBit));
    recv_control.OnPacketRecv(501, MakePacket(21, FrameTypeBit::kStreamBit));
    recv_control.OnPacketRecv(502, MakePacket(22, FrameTypeBit::kAckBit));
    EXPECT_EQ(active_send_calls, 0u);

    // Third ACK-only with no interleaving ack-eliciting packet: counter
    // reached two consecutive (22 was the first of the new run), fires.
    recv_control.OnPacketRecv(503, MakePacket(23, FrameTypeBit::kAckBit));
    EXPECT_EQ(active_send_calls, 1u);
}

TEST(RecvControlTest, EcnCountersReportedInAckEcnFrame) {
    auto timer = std::make_shared<common::TestTimerScheduler>();
    RecvControl recv_control(timer);

    // Two ack-eliciting packets so that ACK is generated later
    recv_control.OnPacketRecv(300, MakePacket(3, FrameTypeBit::kStreamBit));
    recv_control.OnPacketRecv(301, MakePacket(4, FrameTypeBit::kStreamBit));

    // Update ECN counters
    recv_control.OnEcnCounters(0x02, PacketNumberSpace::kApplicationNumberSpace);  // ECT(0)
    recv_control.OnEcnCounters(0x01, PacketNumberSpace::kApplicationNumberSpace);  // ECT(1)
    recv_control.OnEcnCounters(0x03, PacketNumberSpace::kApplicationNumberSpace);  // CE

    auto frame = recv_control.MayGenerateAckFrame(308, PacketNumberSpace::kApplicationNumberSpace, true);
    ASSERT_NE(frame, nullptr);

    auto ack_ecn = std::dynamic_pointer_cast<AckEcnFrame>(frame);
    ASSERT_NE(ack_ecn, nullptr);
    EXPECT_EQ(ack_ecn->GetEct0(), 1u);
    EXPECT_EQ(ack_ecn->GetEct1(), 1u);
    EXPECT_EQ(ack_ecn->GetEcnCe(), 1u);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
