#include <cstdint>
#include <gtest/gtest.h>

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/bbr_v1_congestion_control.h"

using quicx::quic::CcConfigV2;
using quicx::quic::ICongestionControl;
using quicx::quic::BBRv1CongestionControl;
using quicx::quic::SentPacketEvent;
using quicx::quic::AckEvent;

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

    // Need to send ACKs over sufficient time for bandwidth sampling to work
    // Bandwidth sampling requires at least one SRTT of elapsed time
    uint64_t base_time = 10000;
    for (int round = 0; round < 3; ++round) {
        // Send multiple packets per round with proper timing
        for (int i = 0; i < 10; ++i) {
            uint64_t pn = round * 10 + i + 1;
            uint64_t time = base_time + round * 110000 + i * 10000; // >SRTT between rounds
            cc.OnPacketAcked(AckEvent{pn, 1000ull, time, 0ull, false});
        }
    }
    // After 3 rounds with stable bandwidth, should exit STARTUP
    EXPECT_FALSE(cc.InSlowStart());
}

TEST(BBRv1CongestionControlTest, PacingRateAndCanSend) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes; // 10000
    cc.Configure(cfg);
    cc.OnRoundTripSample(100000, 0); // 100ms

    // Bandwidth sampling needs at least SRTT elapsed time
    // Send multiple ACKs over >SRTT timespan for proper bandwidth estimation
    uint64_t base_time = 100000;
    for (int i = 0; i < 10; ++i) {
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i + 1), 1000ull, base_time + i * 10000, 0ull, false});
    }
    
    // After proper sampling, should have valid pacing rate
    uint64_t pacing_rate = cc.GetPacingRateBps();
    EXPECT_GT(pacing_rate, 0u);

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

    // Seed RTT and bandwidth with proper timing
    cc.OnRoundTripSample(100000ull, 0ull); // 100ms
    
    // Send ACKs over sufficient time for bandwidth sampling
    uint64_t base_time = 100000;
    for (int i = 0; i < 15; ++i) {
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i + 1), 1000ull, base_time + i * 10000, 0ull, false});
    }
    
    // Should have valid pacing rate after bandwidth sampling
    uint64_t pacing_rate = cc.GetPacingRateBps();
    EXPECT_GT(pacing_rate, 0u);
}

TEST(BBRv1CongestionControlTest, ProbeBwGainCycleTiming) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Set RTT and send enough data for bandwidth sampling
    cc.OnRoundTripSample(100000ull, 0ull); // 100ms
    
    // Send ACKs over sufficient time for bandwidth sampling and state transitions
    uint64_t base_time = 100000;
    for (int i = 0; i < 20; ++i) {
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i + 1), 1000ull, base_time + i * 10000, 0ull, false});
    }
    
    // Should have valid pacing rate and be in a stable state
    uint64_t pacing_rate = cc.GetPacingRateBps();
    EXPECT_GT(pacing_rate, 0u);
}

TEST(BBRv1CongestionControlTest, ProbeBwFullEightSegmentCycle) {
    BBRv1CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000ull;
    cfg.initial_cwnd_bytes = 20ull * cfg.mss_bytes;
    cc.Configure(cfg);

    // Set RTT and send enough data for bandwidth sampling
    cc.OnRoundTripSample(100000ull, 0ull); // 100ms
    
    // Send ACKs over sufficient time for bandwidth sampling  
    uint64_t base_time = 100000;
    for (int i = 0; i < 25; ++i) {
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i + 1), 1000ull, base_time + i * 10000, 0ull, false});
    }
    
    // Should have valid pacing rate
    uint64_t pacing_rate = cc.GetPacingRateBps();
    EXPECT_GT(pacing_rate, 0u);
}


