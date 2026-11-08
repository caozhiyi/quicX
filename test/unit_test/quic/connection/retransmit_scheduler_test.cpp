#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "quic/connection/controller/retransmit_scheduler.h"

#include "test/unit_test/common/timer/test_timer_scheduler.h"

// P0-C10 / X3: the single-timer, min-deadline slow-path scheduler.
// Invariants under test (design doc §2.1):
//   INV2  cadence is ARITHMETIC (base PTO each time), never exponential
//   INV3  fairness: due slots fire in deadline order, nobody starves
//   INV4  the scheduler itself declares no loss (it only calls the owner)

namespace quicx {
namespace quic {
namespace {

constexpr uint32_t kBasePto = 100;

struct FakeOwner: public IRetransmitOwner {
    explicit FakeOwner(int id, std::vector<std::pair<int, uint32_t>>* log = nullptr):
        id_(id),
        log_(log) {}
    void OnRetransmitDeadline(uint32_t slot, uint64_t now_ms) override {
        fires_.emplace_back(slot, now_ms);
        if (log_) {
            log_->emplace_back(id_, slot);
        }
        if (on_fire_) {
            on_fire_(slot);
        }
    }
    int id_;
    std::vector<std::pair<int, uint32_t>>* log_;
    std::vector<std::pair<uint32_t, uint64_t>> fires_;
    std::function<void(uint32_t)> on_fire_;
};

class RetransmitSchedulerTest: public ::testing::Test {
protected:
    void SetUp() override {
        timers_ = std::make_shared<common::TestTimerScheduler>();
        life_ = std::make_shared<int>(0);
        pto_ = kBasePto;
        sched_ = std::make_unique<RetransmitScheduler>(
            timers_, [this] { return timers_->Now(); }, [this] { return pto_; });
        sched_->SetLifeToken(life_);
    }

    std::shared_ptr<common::TestTimerScheduler> timers_;
    std::shared_ptr<int> life_;
    uint32_t pto_;
    std::unique_ptr<RetransmitScheduler> sched_;
};

TEST_F(RetransmitSchedulerTest, ArmRegistersAtBasePtoAndFiresWhenDue) {
    FakeOwner owner(1);
    const uint64_t t0 = timers_->Now();
    sched_->Arm(&owner, 7);
    EXPECT_TRUE(sched_->IsArmed(&owner, 7));
    // TestTimerScheduler::Now() follows the real monotonic clock, which can
    // tick between our read and the scheduler's: allow a few ms of skew.
    EXPECT_GE(sched_->NextDeadlineMs(), t0 + kBasePto);
    EXPECT_LE(sched_->NextDeadlineMs(), t0 + kBasePto + 5);

    timers_->Advance(kBasePto - 1);
    EXPECT_TRUE(owner.fires_.empty()) << "must not fire before the base PTO elapsed";

    timers_->Advance(2);
    ASSERT_EQ(owner.fires_.size(), 1u);
    EXPECT_EQ(owner.fires_[0].first, 7u);
}

TEST_F(RetransmitSchedulerTest, CadenceIsArithmeticNeverExponential) {
    FakeOwner owner(1);
    sched_->Arm(&owner, 0);

    // The owner never acknowledges anything: it keeps being poked once per
    // base PTO, the same interval every time (INV2) — the property the
    // connection-level PTO backoff (750ms -> 1.5s -> 3s) does NOT have.
    std::vector<uint64_t> gaps;
    uint64_t last = 0;
    for (int i = 0; i < 6; ++i) {
        timers_->Advance(kBasePto + 1);
        ASSERT_EQ(owner.fires_.size(), static_cast<size_t>(i + 1)) << "round " << i;
        const uint64_t at = owner.fires_.back().second;
        if (last != 0) {
            gaps.push_back(at - last);
        }
        last = at;
    }
    for (uint64_t g : gaps) {
        EXPECT_GE(g, kBasePto);
        EXPECT_LE(g, kBasePto + 2u) << "interval must stay at the base PTO, not grow";
    }
}

TEST_F(RetransmitSchedulerTest, ArmIsIdempotentAndKeepsTheEarlierDeadline) {
    FakeOwner owner(1);
    const uint64_t t0 = timers_->Now();
    sched_->Arm(&owner, 0);
    const uint64_t first_deadline = sched_->NextDeadlineMs();
    timers_->Advance(60);
    sched_->Arm(&owner, 0);  // a "new send" with data already outstanding
    // The oldest outstanding data keeps its deadline: continuous sending must
    // not push it out (the keep-alive-PING starvation lesson). Compare against
    // the deadline captured at the FIRST Arm, which is skew-free.
    EXPECT_EQ(sched_->NextDeadlineMs(), first_deadline);
    EXPECT_GE(first_deadline, t0 + kBasePto);
    EXPECT_LE(first_deadline, t0 + kBasePto + 5);
    EXPECT_EQ(sched_->Size(), 1u);

    timers_->Advance(50);
    EXPECT_EQ(owner.fires_.size(), 1u);
}

TEST_F(RetransmitSchedulerTest, RestartPushesTheDeadlineOut) {
    FakeOwner owner(1);
    sched_->Arm(&owner, 0);
    timers_->Advance(80);
    sched_->Restart(&owner, 0);
    timers_->Advance(80);
    EXPECT_TRUE(owner.fires_.empty()) << "restart at +80 must defer the fire past +160";
    timers_->Advance(30);
    EXPECT_EQ(owner.fires_.size(), 1u);
}

TEST_F(RetransmitSchedulerTest, DueSlotsFireInDeadlineOrderAcrossOwners) {
    std::vector<std::pair<int, uint32_t>> log;
    FakeOwner a(1, &log), b(2, &log), c(3, &log);
    sched_->Arm(&b, 0);  // earliest
    timers_->Advance(10);
    sched_->Arm(&c, 0);
    timers_->Advance(10);
    sched_->Arm(&a, 0);

    // One timer serves all of them (min-deadline, not per-owner timers).
    EXPECT_EQ(timers_->PendingCount(), 1u);

    timers_->Advance(kBasePto + 50);  // all three due by now
    ASSERT_GE(log.size(), 3u);
    EXPECT_EQ(log[0].first, 2) << "oldest deadline first (INV3)";
    EXPECT_EQ(log[1].first, 3);
    EXPECT_EQ(log[2].first, 1);
}

TEST_F(RetransmitSchedulerTest, ManyOwnersStillOneTimer) {
    std::vector<std::unique_ptr<FakeOwner>> owners;
    for (int i = 0; i < 50; ++i) {
        owners.push_back(std::make_unique<FakeOwner>(i));
        sched_->Arm(owners.back().get(), 0);
    }
    EXPECT_EQ(sched_->Size(), 50u);
    EXPECT_EQ(timers_->PendingCount(), 1u);
    EXPECT_EQ(timers_->ArmCount(), 1u) << "re-aiming the timer must reuse the node, not allocate";
}

TEST_F(RetransmitSchedulerTest, RemoveCancelsAndEmptyQueueCancelsTheTimer) {
    FakeOwner owner(1);
    sched_->Arm(&owner, 0);
    EXPECT_EQ(timers_->PendingCount(), 1u);

    sched_->Remove(&owner, 0);
    EXPECT_FALSE(sched_->IsArmed(&owner, 0));
    EXPECT_EQ(timers_->PendingCount(), 0u);

    timers_->Advance(kBasePto * 3);
    EXPECT_TRUE(owner.fires_.empty());

    sched_->Remove(&owner, 0);  // idempotent
}

TEST_F(RetransmitSchedulerTest, HandlerRemovingItsSlotStopsTheCadence) {
    FakeOwner owner(1);
    owner.on_fire_ = [&](uint32_t slot) { sched_->Remove(&owner, slot); };
    sched_->Arm(&owner, 0);

    timers_->Advance(kBasePto + 1);
    EXPECT_EQ(owner.fires_.size(), 1u);
    EXPECT_FALSE(sched_->IsArmed(&owner, 0));

    timers_->Advance(kBasePto * 3);
    EXPECT_EQ(owner.fires_.size(), 1u) << "a removed slot must not fire again";
    EXPECT_EQ(timers_->PendingCount(), 0u);
}

TEST_F(RetransmitSchedulerTest, HandlerUpdateOverridesTheAutomaticRestart) {
    FakeOwner owner(1);
    owner.on_fire_ = [&](uint32_t slot) { sched_->Update(&owner, slot, timers_->Now() + 1000); };
    sched_->Arm(&owner, 0);

    timers_->Advance(kBasePto + 1);
    ASSERT_EQ(owner.fires_.size(), 1u);
    timers_->Advance(kBasePto * 3);
    EXPECT_EQ(owner.fires_.size(), 1u) << "handler pushed the slot to +1000ms";
}

TEST_F(RetransmitSchedulerTest, RemoveOwnerDropsEverySlot) {
    FakeOwner owner(1), other(2);
    sched_->Arm(&owner, 0);
    sched_->Arm(&owner, 1);
    sched_->Arm(&owner, 2);
    sched_->Arm(&other, 0);

    sched_->RemoveOwner(&owner);
    EXPECT_FALSE(sched_->IsArmed(&owner, 0));
    EXPECT_FALSE(sched_->IsArmed(&owner, 1));
    EXPECT_FALSE(sched_->IsArmed(&owner, 2));
    EXPECT_TRUE(sched_->IsArmed(&other, 0));
    EXPECT_EQ(sched_->Size(), 1u);

    timers_->Advance(kBasePto + 1);
    EXPECT_TRUE(owner.fires_.empty());
    EXPECT_EQ(other.fires_.size(), 1u);
}

TEST_F(RetransmitSchedulerTest, ClearDropsEverythingAndCancelsTheTimer) {
    FakeOwner owner(1);
    sched_->Arm(&owner, 0);
    sched_->Clear();
    EXPECT_EQ(sched_->Size(), 0u);
    EXPECT_EQ(timers_->PendingCount(), 0u);
    timers_->Advance(kBasePto * 2);
    EXPECT_TRUE(owner.fires_.empty());
}

TEST_F(RetransmitSchedulerTest, NoTimerIsArmedUntilTheLifeTokenIsBound) {
    // An unbound guard is an expired weak_ptr: TimerCore would skip the
    // callback forever, so arming must wait for Bind (SendControl::Bind).
    RetransmitScheduler unbound(timers_, [this] { return timers_->Now(); }, [this] { return pto_; });
    FakeOwner owner(1);
    unbound.Arm(&owner, 0);
    EXPECT_TRUE(unbound.IsArmed(&owner, 0));
    EXPECT_EQ(timers_->PendingCount(), 0u);

    unbound.SetLifeToken(life_);
    EXPECT_EQ(timers_->PendingCount(), 1u);
    timers_->Advance(kBasePto + 1);
    EXPECT_EQ(owner.fires_.size(), 1u);
}

TEST_F(RetransmitSchedulerTest, IntervalFollowsTheProviderEachTimeNotACachedValue) {
    FakeOwner owner(1);
    sched_->Arm(&owner, 0);
    timers_->Advance(kBasePto + 1);
    ASSERT_EQ(owner.fires_.size(), 1u);

    pto_ = 400;  // RTT estimate grew
    sched_->Restart(&owner, 0);
    EXPECT_EQ(sched_->NextDeadlineMs(), timers_->Now() + 400);
}

TEST_F(RetransmitSchedulerTest, NullTimerFacilityIsABookkeepingNoOp) {
    RetransmitScheduler bare(nullptr, [] { return uint64_t(1000); }, [] { return 100u; });
    FakeOwner owner(1);
    bare.Arm(&owner, 3);
    EXPECT_TRUE(bare.IsArmed(&owner, 3));
    EXPECT_EQ(bare.NextDeadlineMs(), 1100u);
    bare.RunDue(1100);
    EXPECT_EQ(owner.fires_.size(), 1u);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
