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

    // Fix min_rtt to 1ms, which defines ProbeBW cycle length
    cc.OnRoundTripSample(1000, 0);
    cc.OnPacketAcked(AckEvent{1, 1000, 1000, 0, false});
    uint64_t bw = 1000 * 1000000ull / 1000ull; // bytes/s

    // Enter ProbeRTT and then exit to ProbeBW (gain=1)
    cc.OnPacketAcked(AckEvent{2, 1000, 1001, 0, false});
    cc.OnPacketAcked(AckEvent{3, 1000, 1001 + 200000, 0, false});
    EXPECT_EQ(cc.GetPacingRateBps(), bw * 1);

    // One cycle later -> gain 1.25
    cc.OnPacketAcked(AckEvent{4, 1000, 1001 + 200000 + 1000, 0, false});
    EXPECT_EQ(cc.GetPacingRateBps(), static_cast<uint64_t>(bw * 1.25));
}

TEST(BBRv2CongestionControlTest, ProbeBwFullEightSegmentCycle) {
    BBRv2CongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000ull;
    cfg.initial_cwnd_bytes = 20ull * cfg.mss_bytes;
    cc.Configure(cfg);

    cc.OnRoundTripSample(1000ull, 0ull);
    cc.OnPacketAcked(AckEvent{1ull, 1000ull, 1000ull, 0ull, false});
    const uint64_t bw = 1000ull * 1000000ull / 1000ull;
    const uint64_t probe_rtt_enter_time = 1001ull;
    const uint64_t probe_bw_start_time = probe_rtt_enter_time + 200000ull;
    cc.OnPacketAcked(AckEvent{2ull, 1000ull, probe_rtt_enter_time, 0ull, false});
    cc.OnPacketAcked(AckEvent{3ull, 1000ull, probe_bw_start_time, 0ull, false});
    ASSERT_EQ(cc.GetPacingRateBps(), bw * 1);

    const double gains[8] = {1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    uint64_t base = probe_bw_start_time;
    for (int i = 0; i < 8; ++i) {
        uint64_t t = base + static_cast<uint64_t>(i + 1) * 1000ull;
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(4 + i), 1000ull, t, 0ull, false});
        uint64_t expected = static_cast<uint64_t>(bw * gains[i]);
        EXPECT_EQ(cc.GetPacingRateBps(), expected) << "cycle index=" << i;
    }
}


