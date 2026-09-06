#include <memory>

#include <gtest/gtest.h>

#include "quic/connection/controller/send_manager.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/ping_frame.h"

#include "test/unit_test/common/timer/test_timer_scheduler.h"

namespace quicx {
namespace quic {
namespace {

// Construction mirrors send_manager_recheck_timer_test.cpp: SendManager only
// needs an ITimer, and the probing-frame query touches nothing else.
class SendManagerProbingFrameTest: public ::testing::Test {
protected:
    void SetUp() override {
        timer_ = std::make_shared<common::TestTimerScheduler>();
        send_manager_ = std::make_unique<SendManager>(timer_);
    }

    std::shared_ptr<common::TestTimerScheduler> timer_;
    std::unique_ptr<SendManager> send_manager_;
};

TEST_F(SendManagerProbingFrameTest, ReportsFalseWhenQueueEmpty) {
    EXPECT_FALSE(send_manager_->HasPendingProbingFrame());
}

TEST_F(SendManagerProbingFrameTest, ReportsTrueForPathChallenge) {
    send_manager_->EnqueueFrame(std::make_shared<PathChallengeFrame>());
    EXPECT_TRUE(send_manager_->HasPendingProbingFrame());
}

TEST_F(SendManagerProbingFrameTest, ReportsTrueForPathResponse) {
    send_manager_->EnqueueFrame(std::make_shared<PathResponseFrame>());
    EXPECT_TRUE(send_manager_->HasPendingProbingFrame());
}

TEST_F(SendManagerProbingFrameTest, ReportsFalseForNonProbingFrame) {
    send_manager_->EnqueueFrame(std::make_shared<PingFrame>());
    EXPECT_FALSE(send_manager_->HasPendingProbingFrame());
}

// Guards the early-return: a probing frame anywhere in the queue counts, not
// just at the head.
TEST_F(SendManagerProbingFrameTest, ReportsTrueWhenProbingFrameMixedWithOthers) {
    send_manager_->EnqueueFrame(std::make_shared<PingFrame>());
    send_manager_->EnqueueFrame(std::make_shared<PathChallengeFrame>());
    send_manager_->EnqueueFrame(std::make_shared<PingFrame>());
    EXPECT_TRUE(send_manager_->HasPendingProbingFrame());
}

}  // namespace
}  // namespace quic
}  // namespace quicx
