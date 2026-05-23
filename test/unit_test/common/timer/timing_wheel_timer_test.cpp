#include <gtest/gtest.h>

#include <random>
#include <set>
#include <vector>

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

// ============================================================================
// Regression suite for the 9-second-silence interop bug (PTO timer livelock).
//
// These tests exercise four blind spots that the original suite missed:
//   1. cascade-boundary fire ordering
//   2. high-frequency Remove+Add of the same task (PTO-style usage)
//   3. min_deadline_cache_ invariants
//   4. large-jump Tick over arbitrary boundaries
// ============================================================================

// ---- (1) Cascade-boundary fire ordering -------------------------------------
//
// L0 boundary: when current_ms_ crosses a multiple of 256 ms, Tick() runs
// Cascade(1, ...) BEFORE firing the slot. A task scheduled at exactly
// boundary+0..15 ms must still fire on time.

TEST(timing_wheel_timer_utest, cascade_l0_boundary_fire) {
    // Use a base aligned to 256 so we can reason about boundaries cleanly.
    // The wheel uses (uint32_t)deadline & kL0Mask, so absolute alignment
    // determines which slot a task lands in.
    uint64_t base = (Now() & ~static_cast<uint64_t>(0xff)) + 0x1000;  // 256-aligned

    int fired_pre = 0, fired_at = 0, fired_post = 0;
    TimerTask tp, ta, tn;
    TimingWheelTimer tw;

    tp.SetTimeoutCallback([&]{ ++fired_pre;  });
    ta.SetTimeoutCallback([&]{ ++fired_at;   });
    tn.SetTimeoutCallback([&]{ ++fired_post; });

    // Bootstrap the wheel at `base`.
    tw.TimerRun(base);

    // schedule:
    //   tp at base + 250  (level 0, near end of current 256-window)
    //   ta at base + 256  (level 1, exactly on boundary -> will cascade to L0 slot 0)
    //   tn at base + 260  (level 1, just past boundary)
    EXPECT_NE(0u, tw.AddTimer(tp, 250, base));
    EXPECT_NE(0u, tw.AddTimer(ta, 256, base));
    EXPECT_NE(0u, tw.AddTimer(tn, 260, base));

    // Walk past all three deadlines in a single Tick — the most stressful path.
    tw.TimerRun(base + 300);

    EXPECT_EQ(1, fired_pre);
    EXPECT_EQ(1, fired_at);
    EXPECT_EQ(1, fired_post);
    EXPECT_TRUE(tw.Empty());
}

TEST(timing_wheel_timer_utest, cascade_l1_boundary_fire) {
    // Cross the L1 boundary: 16384 ms. That triggers Cascade(2, c2) at the
    // moment c0==0 && c1==0.
    constexpr uint64_t kL1 = 16384;
    uint64_t base = (Now() & ~(kL1 - 1)) + (kL1 * 4);  // L1-aligned

    int fired_in_l1 = 0;
    int fired_after = 0;
    TimerTask t1, t2;
    TimingWheelTimer tw;

    t1.SetTimeoutCallback([&]{ ++fired_in_l1; });
    t2.SetTimeoutCallback([&]{ ++fired_after; });

    tw.TimerRun(base);

    // t1 lives in level 2 (>= L1Range)…
    EXPECT_NE(0u, tw.AddTimer(t1, kL1 + 5, base));      // 16389 ms out
    // t2 lives in overflow won't apply here (still < L2Range), but well past L1:
    EXPECT_NE(0u, tw.AddTimer(t2, kL1 * 3 + 7, base));  // ~49 s

    // Big jump straight over the L1 boundary, then a second jump past t2.
    tw.TimerRun(base + kL1 + 100);    // crosses the c1==0 boundary inside Tick
    EXPECT_EQ(1, fired_in_l1);
    EXPECT_EQ(0, fired_after);

    tw.TimerRun(base + kL1 * 4);
    EXPECT_EQ(1, fired_after);
    EXPECT_TRUE(tw.Empty());
}

// ---- (2) PTO-style: high-frequency Remove + Add of the same TimerTask ------
//
// Reproduces the production usage of pto_timer_: every outgoing packet does
//     RemoveTimer(pto_timer_); AddTimer(pto_timer_, 88ms);
// The bug presented as "after ~800 such cycles, the next 88ms fire never came".

TEST(timing_wheel_timer_utest, pto_style_remove_add_thousand_times_then_fire) {
    int fired = 0;
    TimerTask pto;
    TimingWheelTimer tw;
    pto.SetTimeoutCallback([&]{ ++fired; });

    uint64_t now = Now();
    tw.TimerRun(now);

    // First insertion.
    ASSERT_NE(0u, tw.AddTimer(pto, 88, now));

    // Simulate 1500 packets, each tick advances 1 ms (so we sweep through
    // many L0 cascade boundaries 256, 512, 768, 1024, ...).
    for (int i = 0; i < 1500; ++i) {
        now += 1;
        // Drive the wheel BEFORE re-arming, mimicking EventLoop ordering.
        tw.TimerRun(now);

        ASSERT_TRUE(tw.RemoveTimer(pto));
        ASSERT_NE(0u, tw.AddTimer(pto, 88, now));

        // Cache invariant: MinTime must always agree with the freshly armed
        // 88ms deadline (no other tasks present).
        ASSERT_EQ(88, tw.MinTime(now))
            << "MinTime drifted after " << i << " remove+add cycles";
    }

    // Now stop re-arming; the timer MUST fire ~88 ms later.
    EXPECT_EQ(0, fired);
    now += 88;
    tw.TimerRun(now);
    EXPECT_EQ(1, fired) << "PTO timer failed to fire after high-frequency rearm";
    EXPECT_TRUE(tw.Empty());
}

TEST(timing_wheel_timer_utest, pto_style_rearm_across_l1_boundary) {
    // Same as above but engineered so the rearm cycles straddle the
    // 16384 ms L1 cascade boundary — the suspected location of the bug.
    int fired = 0;
    TimerTask pto;
    TimingWheelTimer tw;
    pto.SetTimeoutCallback([&]{ ++fired; });

    constexpr uint64_t kL1 = 16384;
    uint64_t base = (Now() & ~(kL1 - 1)) + kL1 * 2;
    uint64_t now  = base + kL1 - 200;  // start 200ms before an L1 boundary
    tw.TimerRun(now);

    ASSERT_NE(0u, tw.AddTimer(pto, 88, now));

    // 600 cycles of 1ms steps -> traverses base+kL1 boundary and beyond.
    for (int i = 0; i < 600; ++i) {
        now += 1;
        tw.TimerRun(now);
        ASSERT_TRUE(tw.RemoveTimer(pto));
        ASSERT_NE(0u, tw.AddTimer(pto, 88, now));
        ASSERT_EQ(88, tw.MinTime(now))
            << "MinTime drift around L1 boundary, iter=" << i
            << " now=" << now;
    }

    now += 88;
    tw.TimerRun(now);
    EXPECT_EQ(1, fired);
    EXPECT_TRUE(tw.Empty());
}

// ---- (3) min_deadline_cache_ invariants -------------------------------------
//
// After every Add / Remove / Run, MinTime(now) must equal
//    (earliest deadline by brute scan) - now,   or -1 if empty.
// This is the single invariant whose violation explains a 9-second sleep.

namespace {

// Brute-force expected MinTime by externally tracking deadlines.
// Returns -1 if no live tasks.
struct Tracker {
    // task_id -> absolute deadline
    std::unordered_map<uint64_t, uint64_t> live;

    int32_t Expected(uint64_t now) const {
        if (live.empty()) return -1;
        uint64_t earliest = std::numeric_limits<uint64_t>::max();
        for (const auto& kv : live) earliest = std::min(earliest, kv.second);
        if (earliest <= now) return 0;
        uint64_t d = earliest - now;
        if (d > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        return static_cast<int32_t>(d);
    }
};

}  // namespace

TEST(timing_wheel_timer_utest, min_time_cache_invariant_under_random_ops) {
    std::mt19937_64 rng(0xC0FFEEu);
    std::uniform_int_distribution<int>      op_dist(0, 99);
    std::uniform_int_distribution<uint32_t> to_dist(1, 100000);   // up to 100s
    std::uniform_int_distribution<uint32_t> step_dist(0, 50);     // 0..50ms ticks

    TimingWheelTimer tw;
    Tracker tracker;

    // Pool of tasks; we add/remove by index.
    constexpr int kN = 64;
    std::vector<TimerTask> pool(kN);
    std::vector<bool>      alive(kN, false);

    uint64_t now = Now();
    tw.TimerRun(now);

    for (int i = 0; i < kN; ++i) {
        pool[i].SetTimeoutCallback([&, i]{
            // When the wheel fires this task, mirror in tracker.
            tracker.live.erase(pool[i].GetId());
            alive[i] = false;
        });
    }

    for (int iter = 0; iter < 5000; ++iter) {
        int op  = op_dist(rng);
        int idx = op_dist(rng) % kN;

        if (op < 45) {
            // Add (only if dead).
            if (!alive[idx]) {
                uint32_t timeout = to_dist(rng);
                uint64_t id = tw.AddTimer(pool[idx], timeout, now);
                ASSERT_NE(0u, id);
                alive[idx] = true;
                tracker.live[pool[idx].GetId()] = now + timeout;
            }
        } else if (op < 65) {
            // Remove (only if alive).
            if (alive[idx]) {
                ASSERT_TRUE(tw.RemoveTimer(pool[idx]));
                tracker.live.erase(pool[idx].GetId());
                alive[idx] = false;
            }
        } else {
            // Tick.
            now += step_dist(rng);
            tw.TimerRun(now);
            // Drop tracker entries whose deadline has passed (they fired).
            for (auto it = tracker.live.begin(); it != tracker.live.end();) {
                if (it->second <= now) it = tracker.live.erase(it);
                else ++it;
            }
            // Sync alive[] with tracker (fired tasks set alive[i]=false in cb).
        }

        int32_t expected = tracker.Expected(now);
        int32_t actual   = tw.MinTime(now);
        ASSERT_EQ(expected, actual)
            << "MinTime cache invariant broken at iter=" << iter
            << " op=" << op << " idx=" << idx
            << " now=" << now
            << " live=" << tracker.live.size();
    }
}

TEST(timing_wheel_timer_utest, min_time_cache_after_min_task_removed) {
    // Targeted: after RemoveTimer of the cached-min task, MinTime must
    // recompute correctly even if the rebuild path is taken.
    TimerTask t_min, t_mid, t_far;
    TimingWheelTimer tw;
    uint64_t now = Now();
    tw.TimerRun(now);

    tw.AddTimer(t_min, 10,    now);
    tw.AddTimer(t_mid, 500,   now);
    tw.AddTimer(t_far, 50000, now);

    EXPECT_EQ(10,    tw.MinTime(now));
    EXPECT_TRUE(tw.RemoveTimer(t_min));
    EXPECT_EQ(500,   tw.MinTime(now));   // forces dirty -> rescan
    EXPECT_TRUE(tw.RemoveTimer(t_mid));
    EXPECT_EQ(50000, tw.MinTime(now));
    EXPECT_TRUE(tw.RemoveTimer(t_far));
    EXPECT_EQ(-1,    tw.MinTime(now));
}

TEST(timing_wheel_timer_utest, min_time_cache_after_fire_invalidation) {
    // A task firing must invalidate the cache so the next-earliest is reported.
    int fired = 0;
    TimerTask t1, t2;
    TimingWheelTimer tw;
    uint64_t now = Now();
    tw.TimerRun(now);

    t1.SetTimeoutCallback([&]{ ++fired; });
    t2.SetTimeoutCallback([&]{ ++fired; });

    tw.AddTimer(t1, 50,  now);
    tw.AddTimer(t2, 200, now);

    EXPECT_EQ(50, tw.MinTime(now));

    now += 50;
    tw.TimerRun(now);
    EXPECT_EQ(1, fired);

    // After fire, cache must reflect t2's remaining time, not stale t1.
    EXPECT_EQ(150, tw.MinTime(now));
}

// ---- (4) Large-jump Tick over arbitrary boundaries --------------------------
//
// EventLoop may not call TimerRun for several seconds (worker stall, busy
// sender, etc.). When it finally does, current_ms_ jumps far. All in-range
// tasks must fire, none get lost.

TEST(timing_wheel_timer_utest, large_jump_fires_all_due_tasks_in_order) {
    constexpr int kN = 200;
    std::vector<TimerTask> tasks(kN);
    std::vector<int> fire_order;
    fire_order.reserve(kN);

    TimingWheelTimer tw;
    uint64_t now = Now();
    tw.TimerRun(now);

    // Schedule kN tasks at sparse, non-monotonic offsets within ~10s.
    std::mt19937 rng(0xDEAD);
    std::uniform_int_distribution<uint32_t> dist(1, 10000);
    std::vector<uint32_t> deadlines(kN);
    for (int i = 0; i < kN; ++i) {
        deadlines[i] = dist(rng);
        const int idx = i;
        tasks[idx].SetTimeoutCallback([&, idx]{ fire_order.push_back(idx); });
        ASSERT_NE(0u, tw.AddTimer(tasks[idx], deadlines[idx], now));
    }

    // Single huge jump past every deadline.
    now += 10001;
    tw.TimerRun(now);

    // All must fire exactly once.
    EXPECT_EQ(kN, static_cast<int>(fire_order.size()));
    std::set<int> unique(fire_order.begin(), fire_order.end());
    EXPECT_EQ(kN, static_cast<int>(unique.size())) << "duplicate fires";
    EXPECT_TRUE(tw.Empty());

    // Order must be by deadline (ties broken by insertion order is fine, we
    // only check non-decreasing deadlines).
    for (size_t i = 1; i < fire_order.size(); ++i) {
        EXPECT_LE(deadlines[fire_order[i - 1]], deadlines[fire_order[i]])
            << "fire order violates deadline ordering at i=" << i;
    }
}

TEST(timing_wheel_timer_utest, jump_nine_seconds_with_short_timer) {
    // The exact pathology from the interop log: a 88ms timer set, then the
    // wheel does not get ticked for 9 seconds. When it finally ticks,
    // the timer MUST fire (and exactly once).
    int fired = 0;
    TimerTask t;
    TimingWheelTimer tw;
    t.SetTimeoutCallback([&]{ ++fired; });

    uint64_t now = Now();
    tw.TimerRun(now);
    ASSERT_NE(0u, tw.AddTimer(t, 88, now));

    now += 9000;  // 9-second silence
    tw.TimerRun(now);

    EXPECT_EQ(1, fired) << "9-second jump lost the 88ms timer (interop bug)";
    EXPECT_TRUE(tw.Empty());
}

TEST(timing_wheel_timer_utest, reentrant_add_in_callback) {
    // OnPTOTimer re-arms inside the callback. Make sure that's safe and
    // the re-armed timer is NOT fired in the same Tick.
    int fired_outer = 0, fired_inner = 0;
    TimerTask outer, inner;
    TimingWheelTimer tw;

    inner.SetTimeoutCallback([&]{ ++fired_inner; });
    outer.SetTimeoutCallback([&]{
        ++fired_outer;
        tw.AddTimer(inner, 50);  // re-arm during fire
    });

    uint64_t now = Now();
    tw.TimerRun(now);
    tw.AddTimer(outer, 10, now);

    now += 10;
    tw.TimerRun(now);
    EXPECT_EQ(1, fired_outer);
    EXPECT_EQ(0, fired_inner) << "inner fired prematurely";

    // Now drive past inner's deadline.
    now += 60;
    tw.TimerRun(now);
    EXPECT_EQ(1, fired_inner);
    EXPECT_TRUE(tw.Empty());
}

// ---- (Bug #21) Dirty-cache regression --------------------------------------
//
// Repro for the cache corruption that hid a 100 ms flow-control recheck timer
// behind a 10 s idle timer. Sequence:
//   1. A short timer (100 ms) is in the wheel.
//   2. Some other operation marks the cache dirty (e.g. RemoveTimer of the
//      task that previously held the cached min, or a fired slot).
//   3. AddTimer is called with a *larger* deadline (e.g. 10 000 ms idle).
//      The buggy code wrote
//          min_deadline_cache_ = task.time_; cache_dirty_ = false;
//      thereby advertising 10 000 ms as the next-earliest deadline and
//      losing track of the still-pending 100 ms task.
//   4. MinTime() must return 100 ms, not 10 000 ms.

TEST(timing_wheel_timer_utest, min_time_cache_dirty_then_add_larger_deadline) {
    TimerTask t_short, t_holder, t_long;
    TimingWheelTimer tw;
    uint64_t now = Now();
    tw.TimerRun(now);

    // 1. Schedule a short timer (100 ms) — the one that must NOT be hidden.
    tw.AddTimer(t_short, 100, now);

    // Insert and remove an extra task that briefly held the cache, to force
    // RemoveTimer to mark the cache dirty (kInvalidDeadline).
    tw.AddTimer(t_holder, 50, now);              // becomes new cache min
    EXPECT_EQ(50, tw.MinTime(now));              // confirm
    EXPECT_TRUE(tw.RemoveTimer(t_holder));       // -> cache_dirty_ = true

    // 2. Now add a *much later* timer while cache is dirty. Pre-fix code
    //    would set cache = now+10s and clear dirty, hiding the 100 ms task.
    tw.AddTimer(t_long, 10000, now);

    // 3. MinTime must report 100 ms (the still-pending short timer), not
    //    10 000 ms.
    EXPECT_EQ(100, tw.MinTime(now))
        << "Bug #21 regression: AddTimer on dirty cache must not overwrite "
           "an unscanned earlier deadline.";
}

TEST(timing_wheel_timer_utest, min_time_cache_dirty_after_fire_then_add_larger) {
    // Variant of the above: cache is marked dirty by a fired slot (Tick),
    // then AddTimer(larger) must still preserve any remaining shorter task.
    TimerTask t_fired, t_short, t_long;
    TimingWheelTimer tw;
    uint64_t now = Now();
    tw.TimerRun(now);

    tw.AddTimer(t_fired, 10,  now);   // will fire and dirty the cache
    tw.AddTimer(t_short, 100, now);   // must remain visible afterwards

    now += 10;
    tw.TimerRun(now);                 // fires t_fired, sets cache_dirty_

    // Add a far-future task while cache is dirty.
    tw.AddTimer(t_long, 10000, now);

    // Remaining time of t_short = 100 - 10 = 90.
    EXPECT_EQ(90, tw.MinTime(now))
        << "Bug #21 regression (post-fire variant): MinTime must rescan and "
           "find the still-pending short timer.";
}

// Mimics the exact ResetIdleTimer pattern observed in production:
//   - schedule a short recheck (100 ms)
//   - on every ACK arrival: RemoveTimer(idle) + AddTimer(idle, 10 s)
// MinTime must always reflect the recheck timer until the recheck fires,
// regardless of how often the idle timer is rearmed.
TEST(timing_wheel_timer_utest, reset_idle_timer_does_not_hide_short_timer) {
    TimerTask recheck, idle;
    TimingWheelTimer tw;
    uint64_t now = Now();
    tw.TimerRun(now);

    tw.AddTimer(recheck, 100,   now);
    tw.AddTimer(idle,    10000, now);
    EXPECT_EQ(100, tw.MinTime(now));

    // Simulate 5 ACK arrivals over the next 50 ms, each rearming idle.
    for (int i = 0; i < 5; ++i) {
        now += 10;
        EXPECT_TRUE(tw.RemoveTimer(idle));
        tw.AddTimer(idle, 10000, now);

        // Whatever happens on idle, recheck stays the next-earliest.
        int32_t mt = tw.MinTime(now);
        EXPECT_GE(mt, 0);
        EXPECT_LE(mt, 100 - 10 * (i + 1) + 1)
            << "iter " << i << ": recheck must remain the min, got " << mt;
    }
}

}  // namespace
}  // namespace common
}  // namespace quicx
