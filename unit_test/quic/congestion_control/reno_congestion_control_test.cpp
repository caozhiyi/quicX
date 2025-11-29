#include <cinttypes>
#include <cstdint>
#include <gtest/gtest.h>

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/reno_congestion_control.h"

using quicx::quic::CcConfigV2;
using quicx::quic::ICongestionControl;
using quicx::quic::RenoCongestionControl;
using quicx::quic::SentPacketEvent;
using quicx::quic::AckEvent;
using quicx::quic::LossEvent;

TEST(RenoCongestionControlTest, InitialState) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1200;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.max_cwnd_bytes = 1000 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes);
    EXPECT_EQ(cc.GetBytesInFlight(), 0u);
    EXPECT_TRUE(cc.InSlowStart());
    EXPECT_FALSE(cc.InRecovery());
}

TEST(RenoCongestionControlTest, SlowStartIncreasesCwndOnAck) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;
    cc.Configure(cfg);

    // Send and ACK 2k bytes
    SentPacketEvent sp{1, 2000, 100, false};
    cc.OnPacketSent(sp);
    EXPECT_EQ(cc.GetBytesInFlight(), 2000u);

    AckEvent ack{1, 2000, 200, 0, false};
    cc.OnPacketAcked(ack);
    EXPECT_EQ(cc.GetBytesInFlight(), 0u);
    // Slow start: cwnd += acked
    EXPECT_EQ(cc.GetCongestionWindow(), cfg.initial_cwnd_bytes + 2000);
}

TEST(RenoCongestionControlTest, LossEntersRecoveryAndReducesCwnd) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes; // 20000
    cfg.min_cwnd_bytes = 2 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    uint64_t before = cc.GetCongestionWindow();
    // Report a loss of 1000 bytes at time 150
    LossEvent le{2, 1000, 150};
    cc.OnPacketLost(le);

    uint64_t expected_ssthresh = static_cast<uint64_t>(before * cfg.beta);
    if (expected_ssthresh < cfg.min_cwnd_bytes) expected_ssthresh = cfg.min_cwnd_bytes;
    EXPECT_EQ(cc.GetSsthresh(), expected_ssthresh);
    EXPECT_EQ(cc.GetCongestionWindow(), expected_ssthresh);
    EXPECT_FALSE(cc.InSlowStart());
    EXPECT_TRUE(cc.InRecovery());
}

TEST(RenoCongestionControlTest, CongestionAvoidanceGrowthAfterRecovery) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 20 * cfg.mss_bytes;
    cfg.beta = 0.5;
    cc.Configure(cfg);

    // Enter recovery via loss
    cc.OnPacketLost(LossEvent{3, 1000, 100});
    uint64_t cwnd_after_loss = cc.GetCongestionWindow();
    EXPECT_TRUE(cc.InRecovery());

    // ACK after recovery start with greater ack_time to exit recovery
    uint64_t acked = 1000;
    cc.OnPacketAcked(AckEvent{3, acked, 200, 0, false});
    EXPECT_FALSE(cc.InRecovery());

    // In CA: cwnd increases by roughly MSS^2 / cwnd (at least 1)
    uint64_t cwnd_after_ack = cc.GetCongestionWindow();
    EXPECT_GE(cwnd_after_ack, cwnd_after_loss + 1);
}

TEST(RenoCongestionControlTest, CanSendReportsCorrectly) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 5000;
    cc.Configure(cfg);

    uint64_t can_send = 0;
    // Initially no in-flight, should be able to send up to cwnd
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kOk);
    EXPECT_EQ(can_send, cfg.initial_cwnd_bytes);

    // Simulate sending 5k: in-flight == cwnd
    cc.OnPacketSent(SentPacketEvent{10, cfg.initial_cwnd_bytes, 10, false});
    EXPECT_EQ(cc.GetBytesInFlight(), cfg.initial_cwnd_bytes);
    can_send = 1234;
    EXPECT_EQ(cc.CanSend(0, can_send), ICongestionControl::SendState::kBlockedByCwnd);
    EXPECT_EQ(can_send, 0u);
}

TEST(RenoCongestionControlTest, PacingRateComputesFromSrtt) {
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1000;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes; // 10000
    cc.Configure(cfg);

    // SRTT = 100000us (100ms)
    cc.OnRoundTripSample(100000, 0);
    uint64_t expected_bps = (cfg.initial_cwnd_bytes * 8ull * 1000000ull) / 100000ull;
    EXPECT_EQ(cc.GetPacingRateBps(), expected_bps);
}

TEST(RenoCongestionControlTest, BytesInFlightExceedsCwndAfterRecovery) {
    // This test reproduces the issue seen in streaming upload where
    // bytes_in_flight (202395) > cwnd (103036) after entering recovery
    RenoCongestionControl cc;
    CcConfigV2 cfg;
    cfg.mss_bytes = 1460;
    cfg.initial_cwnd_bytes = 10 * cfg.mss_bytes;  // 14600
    cfg.beta = 0.5;
    cc.Configure(cfg);
    
    // Simulate slow start growth
    uint64_t now = 1000;
    for (int i = 0; i < 100; i++) {
        // Send a packet
        cc.OnPacketSent(SentPacketEvent{static_cast<uint64_t>(i), 1460, now, false});
        now += 1;
        
        // ACK it (slow start doubles cwnd)
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i), 1460, now, 0, false});
        cc.OnRoundTripSample(10, 0);  // 10ms RTT
        now += 1;
    }
    
    uint64_t cwnd_before_loss = cc.GetCongestionWindow();
    uint64_t bytes_in_flight_before = cc.GetBytesInFlight();
    
    // Now send many packets without ACKing (simulating network delay)
    for (int i = 100; i < 250; i++) {
        uint64_t can_send = 0;
        if (cc.CanSend(now, can_send) == ICongestionControl::SendState::kOk && can_send >= 1460) {
            cc.OnPacketSent(SentPacketEvent{static_cast<uint64_t>(i), 1460, now, false});
            now += 1;
        }
    }
    
    uint64_t bytes_in_flight_after_send = cc.GetBytesInFlight();
    
    // Trigger packet loss (simulating timeout)
    cc.OnPacketLost(LossEvent{150, 1460, now});
    
    uint64_t cwnd_after_loss = cc.GetCongestionWindow();
    uint64_t bytes_in_flight_after_loss = cc.GetBytesInFlight();
    
    
    // The bug: bytes_in_flight should NEVER exceed cwnd
    // If it does, CanSend will return 0 forever
    EXPECT_LE(bytes_in_flight_after_loss, cwnd_after_loss) 
        << "bytes_in_flight (" << bytes_in_flight_after_loss 
        << ") should not exceed cwnd (" << cwnd_after_loss << ")";
    
    // Verify we can still send after ACKs reduce bytes_in_flight
    for (int i = 100; i < 150; i++) {
        cc.OnPacketAcked(AckEvent{static_cast<uint64_t>(i), 1460, now, 0, false});
        now += 1;
    }
    
    uint64_t can_send = 0;
    auto state = cc.CanSend(now, can_send);
    EXPECT_GT(can_send, 0u) << "Should be able to send after ACKs reduce bytes_in_flight";
}

