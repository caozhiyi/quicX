#include <gtest/gtest.h>

#include <quicx/common/metrics.h>

#include "quic/congestion_control/normal_pacer.h"

namespace quicx {
namespace quic {
namespace {

constexpr uint64_t kRate10MBps = 10 * 1000 * 1000;  // bytes per second

TEST(NormalPacerTest, PacesSubMillisecondGap) {
    // The pacer must keep sub-millisecond pacing gaps: a 1500 B packet at
    // 10 MB/s paces every 150 us. The old millisecond implementation
    // truncated that gap to 0 ms, i.e. no pacing at all at high rates.
    NormalPacer pacer;
    pacer.OnPacingRateUpdated(kRate10MBps);

    const uint64_t t0 = Metrics::NowUs();
    // Drain the burst budget first: one packet larger than the budget.
    pacer.OnPacketSent(t0, 300 * 1024);
    // 1500 B at 10 MB/s -> 150 us until the next send is allowed.
    pacer.OnPacketSent(t0, 1500);

    EXPECT_FALSE(pacer.CanSend(t0 + 149)) << "gap must not collapse below 150 us";
    EXPECT_TRUE(pacer.CanSend(t0 + 150));
}

TEST(NormalPacerTest, BurstBudgetBypassesRate) {
    NormalPacer pacer;
    pacer.OnPacingRateUpdated(1000);  // extremely slow rate
    const uint64_t t0 = Metrics::NowUs();

    // While burst budget remains, sending is allowed regardless of rate.
    EXPECT_TRUE(pacer.CanSend(t0));
    pacer.OnPacketSent(t0, 1024);
    EXPECT_TRUE(pacer.CanSend(t0));
}

TEST(NormalPacerTest, ZeroRateAlwaysAllows) {
    NormalPacer pacer;  // rate never set
    const uint64_t t0 = Metrics::NowUs();
    EXPECT_TRUE(pacer.CanSend(t0));
    pacer.OnPacketSent(t0, 1024);
    EXPECT_TRUE(pacer.CanSend(t0));
    EXPECT_EQ(pacer.TimeUntilSend(), 0);
}

TEST(NormalPacerTest, RefillBudgetOverTime) {
    NormalPacer pacer;
    pacer.OnPacingRateUpdated(kRate10MBps);
    const uint64_t t0 = Metrics::NowUs();

    // Drain the budget, then wait long enough to refill at least one packet.
    pacer.OnPacketSent(t0, 300 * 1024);
    pacer.OnPacketSent(t0, 1500);  // budget exhausted, next send at t0+150us

    EXPECT_FALSE(pacer.CanSend(t0 + 149));
    // After the pacing gap the elapsed time refills the budget again.
    EXPECT_TRUE(pacer.CanSend(t0 + 1000));
}

TEST(NormalPacerTest, ResetClearsState) {
    NormalPacer pacer;
    pacer.OnPacingRateUpdated(kRate10MBps);
    const uint64_t t0 = Metrics::NowUs();
    pacer.OnPacketSent(t0, 300 * 1024);
    pacer.OnPacketSent(t0, 1500);
    EXPECT_FALSE(pacer.CanSend(t0 + 149));

    pacer.Reset();
    EXPECT_TRUE(pacer.CanSend(t0 + 149));  // rate zeroed: always allowed
}

}  // namespace
}  // namespace quic
}  // namespace quicx
