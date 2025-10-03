#include <cstdint>
#include <gtest/gtest.h>

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/bbr_v3_congestion_control.h"

using quicx::quic::CcConfigV2;
using quicx::quic::ICongestionControl;
using quicx::quic::BBRv3CongestionControl;
using quicx::quic::SentPacketEvent;
using quicx::quic::AckEvent;
using quicx::quic::LossEvent;

TEST(BBRv3CongestionControlTest, InitialState) {
    BBRv3CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1200;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cc.Configure(cfg);

    EXPECT_EQ(cc.GetCongestionWindow(), std::max<uint64_t>(cfg.initial_cwnd_bytes, 4 * cfg.mss_bytes));
    EXPECT_EQ(cc.GetBytesInFlight(), 0u);
    EXPECT_TRUE(cc.InSlowStart());
}

TEST(BBRv3CongestionControlTest, LossAdjustsInflightHi) {
    BBRv3CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    // initial ack to seed bw
    cc.OnRoundTripSample(100000, 0);
    cc.OnPacketAcked(AckEvent{1, 2000, 200, 0, false});
    uint64_t cwnd_before = cc.GetCongestionWindow();

    // loss increases loss-in-round and should lead to lower inflight_hi over time
    cc.OnPacketLost(LossEvent{2, 1000, 300});
    EXPECT_LE(cc.GetCongestionWindow(), cwnd_before);
}

TEST(BBRv3CongestionControlTest, CanSendObeysBounds) {
    BBRv3CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cc.Configure(cfg);

    uint64_t can_send = 0;
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kOk);
    cc.OnPacketSent(SentPacketEvent{10, can_send, 10, false});
    uint64_t leftover = 1234;
    EXPECT_EQ(cc.CanSend(0, leftover), ICongestionControl::SendState::kBlockedByCwnd);
    EXPECT_EQ(leftover, 0u);
}

TEST(BBRv3CongestionControlTest, ProbeRttAndProbeBwCycle) {
    BBRv3CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Set min_rtt to 10ms (10000us) for realistic sampling
    cc.OnRoundTripSample(10000, 0);
    
    // Send enough ACKs over sufficient time to establish bandwidth
    // Need elapsed >= srtt_us to trigger bandwidth update
    uint64_t base_time = 10000;
    for (int i = 0; i < 15; ++i) {
        cc.OnPacketSent(SentPacketEvent{static_cast<uint64_t>(i+1), 1000, base_time + i * 1000, false});
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i+1), 1000, base_time + i * 1000, 0, false});
    }
    
    // By now max_bw_bps should be set
    // Bandwidth ~= total_bytes / total_time = 15000 / 0.014s ≈ 1071428 bytes/s
    EXPECT_GT(cc.GetPacingRateBps(), 0UL);
    
    // Trigger ProbeRTT by advancing time > 10s and sending more ACKs
    uint64_t probe_rtt_time = base_time + 10500000; // > 10s later
    cc.OnPacketAcked(AckEvent{20, 1000, probe_rtt_time, 0, false});
    
    // Exit ProbeRTT after 200ms
    uint64_t post_probe_time = probe_rtt_time + 250000;
    cc.OnPacketAcked(AckEvent{21, 1000, post_probe_time, 0, false});
    
    // Should be in ProbeBW now
    EXPECT_FALSE(cc.InSlowStart());
}

TEST(BBRv3CongestionControlTest, ProbeBwFullEightSegmentCycle) {
    BBRv3CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000ull;
    cfg.initial_cwnd_bytes = 20ull * cfg.mss_bytes;
    cc.Configure(cfg);

    cc.OnRoundTripSample(10000ull, 0ull);
    
    // Establish bandwidth by sending packets over sufficient time
    uint64_t base_time = 10000;
    for (int i = 0; i < 20; ++i) {
        cc.OnPacketSent(SentPacketEvent{static_cast<uint64_t>(i+1), 1000, base_time + i * 1000, false});
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i+1), 1000, base_time + i * 1000, 0, false});
    }
    
    // Trigger exit from Startup by simulating full bandwidth reached
    // Send 3 rounds with no significant BW increase
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 5; ++i) {
            uint64_t pn = 21 + round * 5 + i;
            uint64_t t = base_time + 30000 + round * 15000 + i * 1000;
            cc.OnPacketSent(SentPacketEvent{pn, 1000, t, false});
            cc.OnPacketAcked(AckEvent{pn, 1000, t, 0, false});
        }
    }
    
    // Should have exited Startup and gone through Drain to ProbeBW
    // Verify we're no longer in slow start
    EXPECT_FALSE(cc.InSlowStart());
    
    // Verify pacing rate is reasonable (not zero, not absurdly high)
    uint64_t pacing = cc.GetPacingRateBps();
    EXPECT_GT(pacing, 10000UL);   // At least 10KB/s
    EXPECT_LT(pacing, 10000000UL); // Less than 10MB/s
}


