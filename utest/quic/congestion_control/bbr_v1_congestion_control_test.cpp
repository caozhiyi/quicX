#include <cstdint>
#include <gtest/gtest.h>

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/bbr_v1_congestion_control.h"

using quicx::quic::CcConfigV2;
using quicx::quic::ICongestionControl;
using quicx::quic::BBRv1CongestionControl;
using quicx::quic::SentPacketEvent;
using quicx::quic::AckEvent;
using quicx::quic::LossEvent;

TEST(BBRv1CongestionControlTest, InitialState) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1200;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cc.Configure(cfg);

    // BBR initializes cwnd to max(initial, 4*MSS)
    EXPECT_EQ(cc.GetCongestionWindow(), std::max<uint64_t>(cfg.initial_cwnd_bytes, 4 * cfg.mss_bytes));
    EXPECT_EQ(cc.GetBytesInFlight(), 0u);
    EXPECT_TRUE(cc.InSlowStart());
}

TEST(BBRv1CongestionControlTest, BandwidthSampleUpdatesAndDrainAfterStable) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Provide SRTT 100ms
    cc.OnRoundTripSample(100000, 0);

    // Repeated ACKs with same rate -> max_bw stabilizes, exit STARTUP to DRAIN after 3 rounds
    for (int i = 0; i < 3; ++i) {
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i + 1), 1000ull, 1000ull + static_cast<uint64_t>(i), 0ull, false});
    }
    // In our simplified model, stable bandwidth should move from STARTUP to DRAIN
    EXPECT_FALSE(cc.InSlowStart());
}

TEST(BBRv1CongestionControlTest, PacingRateAndCanSend) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes; // 10000
    cc.Configure(cfg);
    cc.OnRoundTripSample(100000, 0); // 100ms

    // With ACKed 1000 over 100ms -> 10kB/s; pacing gain in STARTUP ~2.885
    cc.OnPacketAcked(AckEvent{1ull, 1000ull, 200ull, 0ull, false});
    uint64_t expected_bw = 1000ull * 1000000ull / 100000ull; // bytes/s = 10k
    uint64_t expected_rate = static_cast<uint64_t>(expected_bw * 2.885);
    EXPECT_EQ(cc.GetPacingRateBps(), expected_rate);

    // CanSend when no in-flight
    uint64_t can_send = 0;
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kOk);
    EXPECT_GT(can_send, 0u);

    // Fill inflight to target -> blocked by cwnd
    cc.OnPacketSent(SentPacketEvent{10ull, can_send, 10ull, false});
    uint64_t leftover = 123;
    EXPECT_EQ(cc.CanSend(0, leftover), ICongestionControl::SendState::kBlockedByCwnd);
    EXPECT_EQ(leftover, 0u);
}

TEST(BBRv1CongestionControlTest, ProbeRttTransitionTiming) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Seed RTT and bandwidth
    cc.OnRoundTripSample(100000ull, 0ull); // 100ms
    cc.OnPacketAcked(AckEvent{1ull, 1000ull, 1000ull, 0ull, false});
    uint64_t sample_bw = 1000ull * 1000000ull / 100000ull; // 10k B/s
    // First ACK call enters ProbeRTT (implementation sets pacing_gain=1.0)
    cc.OnPacketAcked(AckEvent{2ull, 500ull, 1200ull, 0ull, false});
    EXPECT_EQ(cc.GetPacingRateBps(), sample_bw * 1); // gain 1.0

    // After ProbeRTT window (200ms) exit to ProbeBW with gain 1.0
    cc.OnPacketAcked(AckEvent{3ull, 500ull, 1400ull + 200000ull, 0ull, false});
    EXPECT_EQ(cc.GetPacingRateBps(), sample_bw * 1);
}

TEST(BBRv1CongestionControlTest, ProbeBwGainCycleTiming) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Enter ProbeBW via ProbeRTT completion
    cc.OnRoundTripSample(1000ull, 0ull); // min_rtt = 1ms sets cycle length
    cc.OnPacketAcked(AckEvent{1ull, 1000ull, 1000ull, 0ull, false}); // seed bw
    uint64_t bw = 1000ull * 1000000ull / 1000ull; // 1,000,000 B/s
    cc.OnPacketAcked(AckEvent{2ull, 1000ull, 1001ull, 0ull, false}); // enter ProbeRTT (gain=1)
    cc.OnPacketAcked(AckEvent{3ull, 1000ull, 1001ull + 200000ull, 0ull, false}); // exit to ProbeBW
    EXPECT_EQ(cc.GetPacingRateBps(), bw * 1);

    // Advance one cycle_len_us => gain 1.25
    cc.OnPacketAcked(AckEvent{4ull, 1000ull, 1001ull + 200000ull + 1000ull, 0ull, false});
    EXPECT_EQ(cc.GetPacingRateBps(), static_cast<uint64_t>(bw * 1.25));

    // Advance another cycle => gain 0.75
    cc.OnPacketAcked(AckEvent{5ull, 1000ull, 1001ull + 200000ull + 2000ull, 0ull, false});
    EXPECT_EQ(cc.GetPacingRateBps(), static_cast<uint64_t>(bw * 0.75));

    // Next cycles => back to 1.0
    cc.OnPacketAcked(AckEvent{6ull, 1000ull, 1001ull + 200000ull + 3000ull, 0ull, false});
    EXPECT_EQ(cc.GetPacingRateBps(), bw * 1);
}

TEST(BBRv1CongestionControlTest, ProbeBwFullEightSegmentCycle) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000ull;
    cfg.initial_cwnd_bytes = 20ull * cfg.mss_bytes;
    cc.Configure(cfg);

    // Set min_rtt=1ms, seed bw sample, enter ProbeRTT then exit to ProbeBW
    cc.OnRoundTripSample(1000ull, 0ull);
    cc.OnPacketAcked(AckEvent{1ull, 1000ull, 1000ull, 0ull, false});
    const uint64_t bw = 1000ull * 1000000ull / 1000ull; // 1,000,000 B/s
    const uint64_t probe_rtt_enter_time = 1001ull;
    const uint64_t probe_bw_start_time = probe_rtt_enter_time + 200000ull; // +200ms
    cc.OnPacketAcked(AckEvent{2ull, 1000ull, probe_rtt_enter_time, 0ull, false});
    cc.OnPacketAcked(AckEvent{3ull, 1000ull, probe_bw_start_time, 0ull, false});
    ASSERT_EQ(cc.GetPacingRateBps(), bw * 1);

    // Expected gains after each cycle advancement from initial ProbeBW state
    const double gains[8] = {0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.25};
    uint64_t base = probe_bw_start_time;
    for (int i = 0; i < 8; ++i) {
        uint64_t t = base + static_cast<uint64_t>(i + 1) * 1000ull; // +1ms per step
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(4 + i), 1000ull, t, 0ull, false});
        uint64_t expected = static_cast<uint64_t>(bw * gains[i]);
        EXPECT_EQ(cc.GetPacingRateBps(), expected) << "cycle index=" << i;
    }
}


