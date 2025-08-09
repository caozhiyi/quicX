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

    // ACK later timestamp to exit recovery and apply cubic growth
    cc.OnPacketAcked(AckEvent{3, 1000, 300, 0, false});
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

    // Set SRTT and verify pacing rate formula cwnd/srtt
    cc.OnRoundTripSample(100000, 0); // 100ms
    const uint64_t expected_bps = (cfg.initial_cwnd_bytes * 8ull * 1000000ull) / 100000ull;
    EXPECT_EQ(cc.GetPacingRateBps(), expected_bps);

    // Fill in-flight up to cwnd
    cc.OnPacketSent(SentPacketEvent{10, cfg.initial_cwnd_bytes, 10, false});
    can_send = 1234;
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kBlockedByCwnd);
    EXPECT_EQ(can_send, 0u);
}


