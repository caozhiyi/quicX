#include <cstdint>
#include <gtest/gtest.h>

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/cubic_congestion_control.h"

using quicx::quic::CcConfigV2;
using quicx::quic::ICongestionControl;
using quicx::quic::CubicCongestionControl;
using quicx::quic::SentPacketEvent;
using quicx::quic::AckEvent;
using quicx::quic::LossEvent;

TEST(CubicCongestionControlTest, InitialState) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1200;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cc.Configure(cfg);

    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes);
    EXPECT_EQ(cc.GetBytesInFlight(), 0u);
    EXPECT_TRUE(cc.InSlowStart());
    EXPECT_FALSE(cc.InRecovery());
}

TEST(CubicCongestionControlTest, SlowStartIncreasesCwndOnAck) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Send and ACK 2k bytes
    cc.OnPacketSent(SentPacketEvent{1, 2000, 100, false});
    EXPECT_EQ(cc.GetBytesInFlight(), 2000u);

    cc.OnPacketAcked(AckEvent{1, 2000, 200, 0, false});
    EXPECT_EQ(cc.GetBytesInFlight(), 0u);
    // Slow start: cwnd += acked
    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes + 2000);
}

TEST(CubicCongestionControlTest, LossReducesCwndAndEntersRecovery) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes; // 20000
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cc.Configure(cfg);

    const uint64_t before = cc.GetCongestionWindow();
    cc.OnPacketLost(LossEvent{2, 1000, 150});

    // CUBIC beta = 0.7 (kept in impl). ssthresh == cwnd after reduction.
    uint64_t expected = static_cast<uint64_t>(before * 0.7);
    if (expected < cfg.min_cwnd_bytes) expected = cfg.min_cwnd_bytes;
    EXPECT_EQ(cc.GetCongestionWindow(), expected);
    EXPECT_EQ(cc.GetSsthresh(), expected);
    EXPECT_TRUE(cc.InRecovery());
    EXPECT_FALSE(cc.InSlowStart());
}

TEST(CubicCongestionControlTest, GrowthAfterRecoveryAck) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 30 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Enter recovery
    cc.OnPacketLost(LossEvent{3, 1500, 100});
    const uint64_t cwnd_after_loss = cc.GetCongestionWindow();
    EXPECT_TRUE(cc.InRecovery());

    // ACK with acked_packet_send_time > recovery_start_time_us_ to exit recovery
    AckEvent post_recovery_ack{};
    post_recovery_ack.pn = 3;
    post_recovery_ack.bytes_acked = 1000;
    post_recovery_ack.ack_time = 300;
    post_recovery_ack.ack_delay = 0;
    post_recovery_ack.ecn_ce = false;
    post_recovery_ack.acked_packet_send_time = 200;  // > recovery_start_time_ (100)
    cc.OnPacketAcked(post_recovery_ack);
    EXPECT_FALSE(cc.InRecovery());
    EXPECT_GE(cc.GetCongestionWindow(), cwnd_after_loss + 1);
}

TEST(CubicCongestionControlTest, CanSendAndPacingRateBasics) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 8000;
    cc.Configure(cfg);

    // No in-flight
    uint64_t can_send = 0;
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kOk);
    EXPECT_EQ(can_send, cfg.initial_cwnd_bytes);

    // Set SRTT and verify pacing rate formula cwnd/srtt * 1.25 (CUBIC uses 1.25x gain)
    cc.OnRoundTripSample(100000, 0); // 100ms
    // bytes/sec = cwnd_bytes * 1.25 / rtt_seconds = cwnd_bytes * 1e6 * 5 / (rtt_us * 4)
    const uint64_t expected_bytes_per_sec = (cfg.initial_cwnd_bytes * 1000000ull * 5) / (100000ull * 4);
    EXPECT_EQ(cc.GetPacingRateBytesPerSec(), expected_bytes_per_sec);

    // Fill in-flight up to cwnd
    cc.OnPacketSent(SentPacketEvent{10, cfg.initial_cwnd_bytes, 10, false});
    can_send = 1234;
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kBlockedByCwnd);
    EXPECT_EQ(can_send, 0u);
}


