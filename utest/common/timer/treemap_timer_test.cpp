#include <gtest/gtest.h>

#include "common/util/time.h"
#include "common/timer/timer.h"

namespace quicx {
namespace common {
namespace {

TEST(treemap_timer_utest, addtimer1) {
    TimerTask t;
    auto timer = MakeTimer();
    EXPECT_TRUE(timer->AddTimer(t, 30));
}

TEST(treemap_timer_utest, addtimer2) {
    TimerTask t1, t2, t3, t4;
    auto timer = MakeTimer();

    EXPECT_TRUE(timer->AddTimer(t1, 10));
    EXPECT_TRUE(timer->AddTimer(t2, 30));
    EXPECT_TRUE(timer->AddTimer(t3, 40));
    EXPECT_TRUE(timer->AddTimer(t4, 50));
}

TEST(treemap_timer_utest, rmtimer) {
    TimerTask t1, t2, t3, t4;
    auto timer = MakeTimer();

    EXPECT_TRUE(timer->AddTimer(t1, 10));
    EXPECT_TRUE(timer->AddTimer(t2, 30));
    EXPECT_TRUE(timer->AddTimer(t3, 40));
    EXPECT_TRUE(timer->AddTimer(t4, 40));

    EXPECT_TRUE(timer->RmTimer(t1));
    EXPECT_TRUE(timer->RmTimer(t2));
    EXPECT_TRUE(timer->RmTimer(t3));
    EXPECT_TRUE(timer->RmTimer(t4));
}

TEST(treemap_timer_utest, mintime) {
    TimerTask t1, t2, t3;
    uint64_t now = UTCTimeMsec();
    auto timer = MakeTimer();

    EXPECT_TRUE(timer->AddTimer(t1, 10 * TimeUnit::kSecond, now));
    EXPECT_TRUE(timer->AddTimer(t2, 30 * TimeUnit::kSecond, now));
    EXPECT_TRUE(timer->AddTimer(t3, 40 * TimeUnit::kMinute, now));

    EXPECT_EQ(10 * TimeUnit::kSecond, timer->MinTime());

    timer->RmTimer(t1);
    EXPECT_EQ(30 * TimeUnit::kSecond, timer->MinTime());

    timer->RmTimer(t2);
    EXPECT_EQ(40 * TimeUnit::kMinute, timer->MinTime());
}

TEST(treemap_timer_utest, timerrun1) {
    TimerTask t1, t2, t3;
    uint64_t now = UTCTimeMsec();
    auto timer = MakeTimer();

    EXPECT_TRUE(timer->AddTimer(t1, 20 * TimeUnit::kMillisecond, now));
    EXPECT_TRUE(timer->AddTimer(t2, 30 * TimeUnit::kSecond, now));
    EXPECT_TRUE(timer->AddTimer(t3, 40 * TimeUnit::kMinute, now));

    EXPECT_EQ(20, timer->MinTime());

    now += 20;
    timer->TimerRun(now);
    EXPECT_EQ(29980, timer->MinTime(now));

    now += 10;
    timer->TimerRun(now);
    EXPECT_EQ(29970, timer->MinTime(now));

    now += 29970;
    timer->TimerRun(now);
    EXPECT_EQ(2370000, timer->MinTime(now));

    EXPECT_FALSE(timer->Empty());
    timer->RmTimer(t3);
    EXPECT_TRUE(timer->Empty());
}

TEST(treemap_timer_utest, timerrun2) {
    TimerTask t1, t2, t3, t4;
    uint64_t now = UTCTimeMsec();
    auto timer = MakeTimer();

    EXPECT_TRUE(timer->AddTimer(t1, 20 * TimeUnit::kMillisecond, now));
    EXPECT_TRUE(timer->AddTimer(t2, 31 * TimeUnit::kSecond, now));
    EXPECT_TRUE(timer->AddTimer(t3, 1 * TimeUnit::kMinute, now));

    EXPECT_EQ(20, timer->MinTime(now));

    now += 20;
    timer->TimerRun(now);
    EXPECT_EQ(30980, timer->MinTime(now));

    now += 10;
    timer->TimerRun(now);
    EXPECT_EQ(30970, timer->MinTime(now));

    now += 30970;
    timer->TimerRun(now);
    EXPECT_EQ(29000, timer->MinTime(now));

    now += 29000;
    timer->TimerRun(now);
    EXPECT_TRUE(timer->Empty());
}

TEST(treemap_timer_utest, timerrun3) {
    TimerTask t1, t2, t3, t4;
    uint64_t now = UTCTimeMsec();
    auto timer = MakeTimer();

    EXPECT_TRUE(timer->AddTimer(t1, 20 * TimeUnit::kMillisecond, now));
    EXPECT_TRUE(timer->AddTimer(t2, 31 * TimeUnit::kSecond, now));

    EXPECT_EQ(20, timer->MinTime(now));

    now += 20;
    timer->TimerRun(now);
    EXPECT_EQ(30980, timer->MinTime(now));

    now += 10;
    timer->TimerRun(now);
    EXPECT_EQ(30970, timer->MinTime(now));

    EXPECT_TRUE(timer->AddTimer(t1, 40 * TimeUnit::kMillisecond, now));
    EXPECT_EQ(40, timer->MinTime(now));

    now += 40;
    timer->TimerRun(now);
    EXPECT_EQ(30930, timer->MinTime(now));

    now += 30930;
    timer->TimerRun(now);
    EXPECT_EQ(-1, timer->MinTime(now));

    EXPECT_TRUE(timer->Empty());
}

}
}
}