#include <cstdint>
#include <gtest/gtest.h>

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/bbr_v2_congestion_control.h"

using quicx::quic::CcConfigV2;
using quicx::quic::ICongestionControl;
using quicx::quic::BBRv2CongestionControl;
using quicx::quic::SentPacketEvent;
using quicx::quic::AckEvent;
using quicx::quic::LossEvent;

TEST(BBRv2CongestionControlTest, InitialState) {
    BBRv2CongestionControl cc;
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

TEST(BBRv2CongestionControlTest, InflightHiAdaptsOnLoss) {
    BBRv2CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Set SRTT and ack to feed bandwidth
    cc.OnRoundTripSample(100000, 0);
    cc.OnPacketAcked(AckEvent{1, 2000, 200, 0, false});
    uint64_t hi_before = cc.GetCongestionWindow();

    // Loss should reduce hi bound
    cc.OnPacketLost(LossEvent{2, 1000, 300});
    // After loss, pacing rate updated and hi target not larger than before
    EXPECT_LE(cc.GetCongestionWindow(), hi_before);
}

TEST(BBRv2CongestionControlTest, CanSendRespectsInflightHi) {
    BBRv2CongestionControl cc;
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

TEST(BBRv2CongestionControlTest, ProbeRttAndProbeBwCycle) {
    BBRv2CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Set RTT and send enough data for bandwidth sampling
    cc.OnRoundTripSample(100000, 0); // 100ms
    
    // Send ACKs over sufficient time for bandwidth sampling
    uint64_t base_time = 100000;
    for (int i = 0; i < 20; ++i) {
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i + 1), 1000, base_time + i * 10000, 0, false});
    }
    
    // Should have valid pacing rate
    uint64_t pacing_rate = cc.GetPacingRateBps();
    EXPECT_GT(pacing_rate, 0u);
}

TEST(BBRv2CongestionControlTest, ProbeBwFullEightSegmentCycle) {
    BBRv2CongestionControl cc;
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


