#include <cstdint>

#include <gtest/gtest.h>

#include "quic/congestion_control/delivery_rate_sampler.h"

// Reconstructed 2026-11-07 after the file was lost to clean_untracked.py;
// semantics follow BBR draft §4.5.2 as implemented in delivery_rate_sampler.h
// and the batch-B acceptance record (P1-CC1).

using quicx::quic::DeliveryRateSampler;
using RateSample = DeliveryRateSampler::RateSample;

TEST(DeliveryRateSamplerTest, IntervalTakesMaxOfSendAndAckElapsed) {
    DeliveryRateSampler s;
    RateSample rs;

    // pn1 at t=0 and pn2 at t=5000 are in flight when pn1 is ACKed at
    // t=20000: send_elapsed=0, ack_elapsed=20000 → interval 20000.
    s.OnPacketSent(1, 1000, 0);
    s.OnPacketSent(2, 1000, 5000);
    s.OnPacketAcked(1, 1000, 20000, rs);
    EXPECT_TRUE(rs.valid);
    EXPECT_EQ(rs.delivery_rate_bps, 1000ull * 1000000 / 20000);

    // pn3 sent at t=20100 while pn2 (sent @5000) is still in flight:
    // first_sent_time=5000, last-delivery snapshot=20000. Its ACK at
    // t=20300 lands only 300us after the previous delivery, so
    // ack_elapsed=300 and the send span 20100-5000=15100 wins the max().
    // The first (buggy) implementation used ack_time - first_sent_time
    // = 15300, understating the rate by the inter-delivery gap.
    s.OnPacketSent(3, 1000, 20100);
    s.OnPacketAcked(2, 1000, 20200, rs);
    s.OnPacketAcked(3, 1000, 20300, rs);
    EXPECT_TRUE(rs.valid);
    // delivered delta = 3000 - 1000 (pn3 snapshotted delivered=1000).
    EXPECT_EQ(rs.delivery_rate_bps, 2000ull * 1000000 / 15100);
}

TEST(DeliveryRateSamplerTest, AckElapsedDominatesWhenSendIsInstant) {
    DeliveryRateSampler s;
    RateSample rs;

    // A lone packet sent after the previous delivery: send_elapsed=0, so
    // the interval is the time since that delivery (ack side wins).
    s.OnPacketSent(1, 500, 0);
    s.OnPacketAcked(1, 500, 8000, rs);
    EXPECT_TRUE(rs.valid);
    EXPECT_EQ(rs.delivery_rate_bps, 500ull * 1000000 / 8000);

    s.OnPacketSent(2, 500, 8100);  // snapshot: last delivery at 8000
    s.OnPacketAcked(2, 500, 9000, rs);
    EXPECT_TRUE(rs.valid);
    // ack_elapsed = 9000 - 8000 = 1000; delivered delta = 1000 - 500.
    EXPECT_EQ(rs.delivery_rate_bps, 500ull * 1000000 / 1000);
}

TEST(DeliveryRateSamplerTest, AppLimitedFlagPropagatesFromSnapshotTime) {
    DeliveryRateSampler s;
    RateSample rs;

    s.SetAppLimited();
    EXPECT_TRUE(s.IsAppLimited());
    s.OnPacketSent(1, 1000, 0);  // snapshot carries is_app_limited=true

    s.ClearAppLimited();
    EXPECT_FALSE(s.IsAppLimited());
    s.OnPacketSent(2, 1000, 1000);  // snapshot carries false

    s.OnPacketAcked(1, 1000, 5000, rs);
    EXPECT_TRUE(rs.valid);
    EXPECT_TRUE(rs.is_app_limited);

    s.OnPacketAcked(2, 1000, 6000, rs);
    EXPECT_TRUE(rs.valid);
    EXPECT_FALSE(rs.is_app_limited);
}

TEST(DeliveryRateSamplerTest, DuplicateAndUnknownAcksYieldInvalidSamples) {
    DeliveryRateSampler s;
    RateSample rs;

    s.OnPacketSent(1, 1000, 0);
    s.OnPacketAcked(1, 1000, 4000, rs);
    EXPECT_TRUE(rs.valid);
    EXPECT_EQ(s.delivered_bytes(), 1000u);

    // Duplicate ACK of the same packet...
    RateSample dup;
    s.OnPacketAcked(1, 1000, 4500, dup);
    EXPECT_FALSE(dup.valid);
    EXPECT_EQ(dup.delivery_rate_bps, 0u);

    // ...and an ACK for a PN we never snapshotted.
    s.OnPacketAcked(99, 1000, 4600, dup);
    EXPECT_FALSE(dup.valid);

    // Neither may disturb the accounting.
    EXPECT_EQ(s.delivered_bytes(), 1000u);
    EXPECT_EQ(s.tracked_packets(), 0u);
}

TEST(DeliveryRateSamplerTest, LostPacketsAreExcludedFromDeliveredBytes) {
    DeliveryRateSampler s;
    RateSample rs;

    s.OnPacketSent(1, 1000, 0);
    s.OnPacketSent(2, 1000, 1000);

    s.OnPacketLost(1);
    EXPECT_EQ(s.tracked_packets(), 1u);

    s.OnPacketAcked(2, 1000, 6000, rs);
    EXPECT_TRUE(rs.valid);
    // Lost bytes never entered the delivered counter.
    EXPECT_EQ(s.delivered_bytes(), 1000u);
    EXPECT_EQ(s.tracked_packets(), 0u);
}
