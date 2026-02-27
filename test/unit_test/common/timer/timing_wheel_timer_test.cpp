#include <gtest/gtest.h>

#include "common/util/time.h"
#include "common/timer/timing_wheel_timer.h"

namespace quicx {
namespace common {
namespace {

// ---- helpers ----------------------------------------------------------------

static uint64_t Now() { return UTCTimeMsec(); }

// ---- AddTimer ---------------------------------------------------------------

TEST(timing_wheel_timer_utest, add_single) {
    TimerTask t;
    TimingWheelTimer tw;
    EXPECT_NE(0u, tw.AddTimer(t, 30));
}

TEST(timing_wheel_timer_utest, add_multiple) {
    TimerTask t1, t2, t3, t4;
    TimingWheelTimer tw;
    uint64_t now = Now();
    EXPECT_NE(0u, tw.AddTimer(t1, 10,  now));
    EXPECT_NE(0u, tw.AddTimer(t2, 30,  now));
    EXPECT_NE(0u, tw.AddTimer(t3, 40,  now));
    EXPECT_NE(0u, tw.AddTimer(t4, 50,  now));
    EXPECT_FALSE(tw.Empty());
}

// ---- RemoveTimer ------------------------------------------------------------

TEST(timing_wheel_timer_utest, remove_all) {
    TimerTask t1, t2, t3, t4;
    TimingWheelTimer tw;
    uint64_t now = Now();

    tw.AddTimer(t1, 10,  now);
    tw.AddTimer(t2, 30,  now);
    tw.AddTimer(t3, 40,  now);
    tw.AddTimer(t4, 40,  now);

    EXPECT_TRUE(tw.RemoveTimer(t1));
    EXPECT_TRUE(tw.RemoveTimer(t2));
    EXPECT_TRUE(tw.RemoveTimer(t3));
    EXPECT_TRUE(tw.RemoveTimer(t4));
    EXPECT_TRUE(tw.Empty());
}

TEST(timing_wheel_timer_utest, remove_invalid) {
    TimerTask t;
    TimingWheelTimer tw;
    // Never added — should return false
    EXPECT_FALSE(tw.RemoveTimer(t));
}

// ---- MinTime ----------------------------------------------------------------

TEST(timing_wheel_timer_utest, min_time_empty) {
    TimingWheelTimer tw;
    uint64_t now = Now();
    EXPECT_EQ(-1, tw.MinTime(now));
}

TEST(timing_wheel_timer_utest, min_time_single) {
    TimerTask t;
    TimingWheelTimer tw;
    uint64_t now = Now();

    tw.AddTimer(t, 10 * TimeUnit::kSecond, now);
    EXPECT_EQ(10 * TimeUnit::kSecond, tw.MinTime(now));
}

TEST(timing_wheel_timer_utest, min_time_multiple) {
    TimerTask t1, t2, t3;
    TimingWheelTimer tw;
    uint64_t now = Now();

    tw.AddTimer(t1, 10 * TimeUnit::kSecond,  now);
    tw.AddTimer(t2, 30 * TimeUnit::kSecond,  now);
    tw.AddTimer(t3, 40 * TimeUnit::kMinute,  now);

    EXPECT_EQ(10 * TimeUnit::kSecond, tw.MinTime(now));

    tw.RemoveTimer(t1);
    EXPECT_EQ(30 * TimeUnit::kSecond, tw.MinTime(now));

    tw.RemoveTimer(t2);
    EXPECT_EQ(40 * TimeUnit::kMinute, tw.MinTime(now));
}

// ---- TimerRun — callbacks fire correctly ------------------------------------

TEST(timing_wheel_timer_utest, timerrun_basic) {
    int fired = 0;
    TimerTask t1, t2, t3;
    TimingWheelTimer tw;
    uint64_t now = Now();

    t1.SetTimeoutCallback([&]{ ++fired; });
    t2.SetTimeoutCallback([&]{ ++fired; });
    t3.SetTimeoutCallback([&]{ ++fired; });

    tw.AddTimer(t1, 20,  now);
    tw.AddTimer(t2, 30 * TimeUnit::kSecond,  now);
    tw.AddTimer(t3, 40 * TimeUnit::kMinute,  now);

    EXPECT_EQ(20, tw.MinTime(now));

    // advance 20 ms — t1 fires
    now += 20;
    tw.TimerRun(now);
    EXPECT_EQ(1, fired);
    EXPECT_EQ(29980, tw.MinTime(now));

    // advance 10 ms — nothing new
    now += 10;
    tw.TimerRun(now);
    EXPECT_EQ(1, fired);
    EXPECT_EQ(29970, tw.MinTime(now));

    // advance to t2 expiry
    now += 29970;
    tw.TimerRun(now);
    EXPECT_EQ(2, fired);
    EXPECT_EQ(2370000, tw.MinTime(now));

    EXPECT_FALSE(tw.Empty());
    tw.RemoveTimer(t3);
    EXPECT_TRUE(tw.Empty());
}

TEST(timing_wheel_timer_utest, timerrun_all_expire) {
    int fired = 0;
    TimerTask t1, t2, t3;
    TimingWheelTimer tw;
    uint64_t now = Now();

    t1.SetTimeoutCallback([&]{ ++fired; });
    t2.SetTimeoutCallback([&]{ ++fired; });
    t3.SetTimeoutCallback([&]{ ++fired; });

    tw.AddTimer(t1, 20,                     now);
    tw.AddTimer(t2, 31 * TimeUnit::kSecond, now);
    tw.AddTimer(t3,  1 * TimeUnit::kMinute, now);

    now += 20;   tw.TimerRun(now);  EXPECT_EQ(1, fired);
    now += 10;   tw.TimerRun(now);
    now += 30970; tw.TimerRun(now); EXPECT_EQ(2, fired);
    now += 29000; tw.TimerRun(now); EXPECT_EQ(3, fired);

    EXPECT_TRUE(tw.Empty());
}

TEST(timing_wheel_timer_utest, timerrun_readd) {
    int fired = 0;
    TimerTask t1, t2;
    TimingWheelTimer tw;
    uint64_t now = Now();

    t1.SetTimeoutCallback([&]{ ++fired; });
    t2.SetTimeoutCallback([&]{ ++fired; });

    tw.AddTimer(t1, 20,                     now);
    tw.AddTimer(t2, 31 * TimeUnit::kSecond, now);

    now += 20;   tw.TimerRun(now);  EXPECT_EQ(1, fired);
    now += 10;   tw.TimerRun(now);

    // Re-add t1 with a fresh timeout
    tw.AddTimer(t1, 40, now);
    EXPECT_EQ(40, tw.MinTime(now));

    now += 40;   tw.TimerRun(now);  EXPECT_EQ(2, fired);

    now += 30930; tw.TimerRun(now); EXPECT_EQ(3, fired);

    EXPECT_EQ(-1, tw.MinTime(now));
    EXPECT_TRUE(tw.Empty());
}

// ---- Long timeout (overflow list) ------------------------------------------

TEST(timing_wheel_timer_utest, long_timeout_overflow) {
    int fired = 0;
    TimerTask t;
    TimingWheelTimer tw;
    uint64_t now = Now();

    t.SetTimeoutCallback([&]{ ++fired; });

    // 2 hours — beyond L2Range (~17.5 min) → goes into overflow list
    tw.AddTimer(t, 2 * TimeUnit::kHour, now);
    EXPECT_FALSE(tw.Empty());

    // Should not fire before the deadline
    now += 1 * TimeUnit::kHour;
    tw.TimerRun(now);
    EXPECT_EQ(0, fired);
    EXPECT_FALSE(tw.Empty());

    // Jump past the deadline
    now += 2 * TimeUnit::kHour;
    tw.TimerRun(now);
    EXPECT_EQ(1, fired);
    EXPECT_TRUE(tw.Empty());
}

// ---- Immediate / already-expired timer -------------------------------------

TEST(timing_wheel_timer_utest, immediate_timer) {
    int fired = 0;
    TimerTask t;
    TimingWheelTimer tw;
    uint64_t now = Now();

    t.SetTimeoutCallback([&]{ ++fired; });
    tw.AddTimer(t, 0, now);   // expires immediately

    tw.TimerRun(now);
    EXPECT_EQ(1, fired);
    EXPECT_TRUE(tw.Empty());
}

// ---- Multiple tasks in same slot -------------------------------------------

TEST(timing_wheel_timer_utest, same_slot_multiple_tasks) {
    int fired = 0;
    TimerTask t1, t2, t3;
    TimingWheelTimer tw;
    uint64_t now = Now();

    t1.SetTimeoutCallback([&]{ ++fired; });
    t2.SetTimeoutCallback([&]{ ++fired; });
    t3.SetTimeoutCallback([&]{ ++fired; });

    // All three land in the same 1-ms resolution slot
    tw.AddTimer(t1, 100, now);
    tw.AddTimer(t2, 100, now);
    tw.AddTimer(t3, 100, now);

    now += 100;
    tw.TimerRun(now);
    EXPECT_EQ(3, fired);
    EXPECT_TRUE(tw.Empty());
}

}  // namespace
}  // namespace common
}  // namespace quicx
