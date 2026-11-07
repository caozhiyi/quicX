#include <cstdint>

#include <gtest/gtest.h>

#include "quic/congestion_control/cubic_congestion_control.h"
#include "quic/congestion_control/if_congestion_control.h"

using quicx::quic::AckEvent;
using quicx::quic::CcConfigV2;
using quicx::quic::CubicCongestionControl;
using quicx::quic::ICongestionControl;
using quicx::quic::LossEvent;
using quicx::quic::SentPacketEvent;

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

    // Fill the initial window (P1-CC4: an ACK for a packet that left with
    // more than half the window unused is app-limited and must not grow
    // the cwnd — so the sender has to be cwnd-limited for growth).
    for (uint64_t pn = 1; pn <= 5; ++pn) {
        cc.OnPacketSent(SentPacketEvent{pn, 2000, 100, false});
    }
    EXPECT_EQ(cc.GetBytesInFlight(), 10000u);

    // ACK a packet that was NOT app-limited: packets 1-2 left with less than
    // half the window in flight (2KB/4KB out of 10KB); packet 3 onward did not.
    cc.OnPacketAcked(AckEvent{3, 2000, 200, 0, false, /*send_time_us=*/100});
    EXPECT_EQ(cc.GetBytesInFlight(), 8000u);
    // Slow start: cwnd += acked
    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes + 2000);
}

// P1-CC4: packets sent while the application leaves more than half the
// window unused are app-limited; acknowledging them must not grow cwnd.
TEST(CubicCongestionControlTest, AppLimitedAckDoesNotGrowCwnd) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cc.Configure(cfg);

    // A single 2KB packet out of a 10KB window: app-limited by construction
    // (even after sending, less than half the window is in flight).
    cc.OnPacketSent(SentPacketEvent{1, 2000, 100, false});
    cc.OnPacketAcked(AckEvent{1, 2000, 200, 0, false, 100});
    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes) << "app-limited ACK must not grow cwnd";

    // Once packets genuinely fill the window, growth resumes (ACK a packet
    // that was sent with the window on its way to being full).
    for (uint64_t pn = 2; pn <= 6; ++pn) {
        cc.OnPacketSent(SentPacketEvent{pn, 2000, 100, false});
    }
    cc.OnPacketAcked(AckEvent{6, 2000, 300, 0, false, 100});
    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes + 2000);
}

TEST(CubicCongestionControlTest, LossReducesCwndAndEntersRecovery) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;  // 20000
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
    cc.OnRoundTripSample(100000, 100000, 100000, 0);  // 100ms
    // bytes/sec = cwnd_bytes * 1.25 / rtt_seconds = cwnd_bytes * 1e6 * 5 / (rtt_us * 4)
    const uint64_t expected_bytes_per_sec = (cfg.initial_cwnd_bytes * 1000000ull * 5) / (100000ull * 4);
    EXPECT_EQ(cc.GetPacingRateBytesPerSec(), expected_bytes_per_sec);

    // Fill in-flight up to cwnd
    cc.OnPacketSent(SentPacketEvent{10, cfg.initial_cwnd_bytes, 10, false});
    can_send = 1234;
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kBlockedByCwnd);
    EXPECT_EQ(can_send, 0u);
}

// Regression (RFC 9438 §4.2): the cubic power must stay signed. With
// W_max = 100 pkts, β = 0.7, C = 0.4, a loss leaves cwnd at 70 pkts and
// K = cbrt(30/0.4) ≈ 4.22s. At t = 0 the spec gives W_cubic(0) =
// C*(0-K)^3 + W_max = 70 pkts — equal to the post-loss cwnd. The old
// |t-K| bug mirrored the concave half above W_max and jumped cwnd to
// ~130 pkts on the very first ACK after a loss.
TEST(CubicCongestionControlTest, ConcaveRegionStaysBelowWMaxAfterLoss) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 100 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cc.Configure(cfg);

    cc.OnPacketLost(LossEvent{1, 1000, 100});  // cwnd: 100 -> 70 pkts
    const uint64_t cwnd_after_loss = cc.GetCongestionWindow();
    EXPECT_EQ(cwnd_after_loss, 70u * cfg.mss_bytes);

    AckEvent ack{};
    ack.pn = 2;
    ack.bytes_acked = 1000;
    ack.ack_time = 300;
    ack.ack_delay = 0;
    ack.ecn_ce = false;
    ack.acked_packet_send_time = 200;  // > recovery start (100)
    cc.OnPacketAcked(ack);

    // Must stay below the pre-loss window (old bug: ~130 pkts) and near
    // the post-loss cwnd (only the Reno-friendly increment applies).
    EXPECT_LT(cc.GetCongestionWindow(), 100u * cfg.mss_bytes);
    EXPECT_NEAR(cc.GetCongestionWindow(), cwnd_after_loss, cfg.mss_bytes);
}

// Counterpart of the above: once t passes K, W_cubic probes ABOVE W_max
// (convex region) — removing the |t-K| fold must not kill growth.
TEST(CubicCongestionControlTest, ConvexRegionProbesPastWMaxAfterK) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 100 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cc.Configure(cfg);

    cc.OnPacketLost(LossEvent{1, 1000, 100});  // 70 pkts, W_max = 100

    AckEvent ack{};
    ack.pn = 2;
    ack.bytes_acked = 1000;
    ack.ack_time = 300;
    ack.ack_delay = 0;
    ack.ecn_ce = false;
    ack.acked_packet_send_time = 200;
    cc.OnPacketAcked(ack);  // epoch starts at t = 300us

    // t = 6s > K ≈ 4.22s: W_cubic = 0.4*(6-4.22)^3 + 100 ≈ 102.3 pkts.
    AckEvent probe{};
    probe.pn = 3;
    probe.bytes_acked = 1000;
    probe.ack_time = 300 + 6000000;
    probe.ack_delay = 0;
    probe.ecn_ce = false;
    probe.acked_packet_send_time = 6000000;
    cc.OnPacketAcked(probe);

    EXPECT_GT(cc.GetCongestionWindow(), 100u * cfg.mss_bytes);
}

// Regression (RFC 9438 §4.7): fast convergence must set
// W_max = cwnd * (1 + β_cubic)/2 = 0.85 * cwnd, not 0.65 * cwnd.
TEST(CubicCongestionControlTest, FastConvergenceUsesOnePlusBetaOverTwo) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 100 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Loss #1: cwnd(100) >= W_max(100) → plain update, W_max = 100.
    cc.OnPacketLost(LossEvent{1, 1000, 100});
    EXPECT_NEAR(cc.GetWMaxPkts(), 100.0, 0.5);

    // Recover partially: first post-recovery ACK leaves cwnd ≈ 70 pkts.
    AckEvent ack{};
    ack.pn = 2;
    ack.bytes_acked = 1000;
    ack.ack_time = 300;
    ack.ack_delay = 0;
    ack.ecn_ce = false;
    ack.acked_packet_send_time = 200;
    cc.OnPacketAcked(ack);
    const double cwnd_pkts_before_loss2 =
        static_cast<double>(cc.GetCongestionWindow()) / cfg.mss_bytes;

    // Loss #2 while still below W_max → fast convergence kicks in.
    cc.OnPacketLost(LossEvent{2, 1000, 400});
    EXPECT_NEAR(cc.GetWMaxPkts(), 0.85 * cwnd_pkts_before_loss2, 0.05);
}

// Same coefficient check via the ECN-CE congestion path.
TEST(CubicCongestionControlTest, FastConvergenceEcnPathMatchesLossPath) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 100 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cc.Configure(cfg);

    // ECN event #1: cwnd(100) >= W_max(100) → W_max = 100, cwnd -> 70.
    AckEvent ecn1{};
    ecn1.pn = 1;
    ecn1.bytes_acked = 1000;
    ecn1.ack_time = 100;
    ecn1.ack_delay = 0;
    ecn1.ecn_ce = true;
    cc.OnPacketAcked(ecn1);
    EXPECT_TRUE(cc.InRecovery());
    EXPECT_NEAR(cc.GetWMaxPkts(), 100.0, 0.5);

    // Exit recovery; cwnd ≈ 70 pkts, still below W_max.
    AckEvent ack{};
    ack.pn = 2;
    ack.bytes_acked = 1000;
    ack.ack_time = 300;
    ack.ack_delay = 0;
    ack.ecn_ce = false;
    ack.acked_packet_send_time = 200;
    cc.OnPacketAcked(ack);
    const double cwnd_pkts_before_ecn2 =
        static_cast<double>(cc.GetCongestionWindow()) / cfg.mss_bytes;

    // ECN event #2 below W_max → 0.85 factor, same as the loss path.
    AckEvent ecn2{};
    ecn2.pn = 3;
    ecn2.bytes_acked = 1000;
    ecn2.ack_time = 500;
    ecn2.ack_delay = 0;
    ecn2.ecn_ce = true;
    cc.OnPacketAcked(ecn2);
    EXPECT_NEAR(cc.GetWMaxPkts(), 0.85 * cwnd_pkts_before_ecn2, 0.05);
}

// P1-CC4 (RFC 9406 §4.2): the delay-increase signal (current round min RTT
// ≥ last round min + clamp(last/8, [4ms, 16ms])) exits slow start into CSS,
// and a transient RTT spike reverts to full slow start.
TEST(CubicCongestionControlTest, HyStartDelayIncreaseEntersCssAndReverts) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;  // above kHyStartLowWindow
    cc.Configure(cfg);

    // One round: 8 packets sent, then ACKed at a fixed RTT (sample count
    // reaches kHyStartMinSamples within the round).
    auto round = [&](uint64_t base_pn, uint64_t rtt_us) {
        for (uint64_t i = 0; i < 8; ++i) {
            cc.OnPacketSent(SentPacketEvent{base_pn + i, 2000, 100, false});
        }
        for (uint64_t i = 0; i < 8; ++i) {
            cc.OnRoundTripSample(rtt_us, rtt_us, rtt_us, 0);
            cc.OnPacketAcked(AckEvent{base_pn + i, 2000, 200, 0, false, 100});
        }
    };

    round(1, 10000);   // round 1 baseline (10ms); no last-round min yet
    round(9, 10000);   // round 2: level RTT, no signal
    round(17, 15000);  // round 3: 15ms >= 10ms + clamp(10ms/8→4ms) → CSS
    round(25, 10500);  // CSS round: RTT back below the 15ms baseline → revert
    EXPECT_TRUE(cc.InSlowStart()) << "transient RTT spike must revert to slow start";
}

// P1-CC4 (RFC 9406 §4.2): CSS runs for at most CSS_ROUNDS (5) rounds; if the
// RTT inflation persists the whole time, slow start commits to congestion
// avoidance with ssthresh at the current cwnd.
TEST(CubicCongestionControlTest, HyStartCssExpiresToCongestionAvoidance) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    auto round = [&](uint64_t base_pn, uint64_t rtt_us) {
        for (uint64_t i = 0; i < 8; ++i) {
            cc.OnPacketSent(SentPacketEvent{base_pn + i, 2000, 100, false});
        }
        for (uint64_t i = 0; i < 8; ++i) {
            cc.OnRoundTripSample(rtt_us, rtt_us, rtt_us, 0);
            cc.OnPacketAcked(AckEvent{base_pn + i, 2000, 200, 0, false, 100});
        }
    };

    round(1, 10000);
    round(9, 10000);
    round(17, 15000);  // inflated round (signal fires on the next window close)
    // The inflating round closes at round(25)'s first ACK → CSS entry;
    // five inflated window closes later it commits to congestion avoidance
    // (no revert: 15ms is not < the 15ms baseline).
    round(25, 15000);
    round(33, 15000);
    round(41, 15000);
    round(49, 15000);
    round(57, 15000);
    EXPECT_FALSE(cc.InSlowStart()) << "persisted RTT inflation must commit to congestion avoidance";
}

// P1-CC4 (RFC 9406 §4.2): round delimiters are packet-number based; the
// delay-increase check runs when the inflated round's window end is ACKed.
TEST(CubicCongestionControlTest, HyStartCssRoundByRound) {
    CubicCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    auto round = [&](uint64_t base_pn, uint64_t rtt_us) {
        for (uint64_t i = 0; i < 8; ++i) {
            cc.OnPacketSent(SentPacketEvent{base_pn + i, 2000, 100, false});
        }
        for (uint64_t i = 0; i < 8; ++i) {
            cc.OnRoundTripSample(rtt_us, rtt_us, rtt_us, 0);
            cc.OnPacketAcked(AckEvent{base_pn + i, 2000, 200, 0, false, 100});
        }
    };

    round(1, 10000);
    round(9, 10000);
    round(17, 15000);
    // The inflated round's samples are only evaluated when its window end is
    // ACKed — i.e. at round(25)'s first ACK.
    EXPECT_FALSE(cc.InCss()) << "CSS cannot fire before the round closes";
    round(25, 15000);
    EXPECT_TRUE(cc.InCss()) << "round(25)'s window close must enter CSS";
    round(33, 15000);
    round(41, 15000);
    round(49, 15000);
    EXPECT_TRUE(cc.InCss()) << "css_rounds=" << cc.GetCssRounds();
    round(57, 15000);
    EXPECT_FALSE(cc.InSlowStart()) << "css_rounds=" << cc.GetCssRounds();
}
