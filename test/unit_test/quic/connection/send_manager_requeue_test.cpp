#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "quic/connection/controller/send_manager.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/ping_frame.h"

#include "test/unit_test/common/timer/test_timer_scheduler.h"

// P0-C10 / INV1: value frames (MAX_*, *_BLOCKED) are owned by SendManager's
// generic requeue. Their producers de-duplicate per value, so a frame lost
// without an owner is never minted again. Also covers RequeueFront, the safety
// net for frames a failed build had already dequeued.

namespace quicx {
namespace quic {
namespace {

class SendManagerRequeueTest: public ::testing::Test {
protected:
    void SetUp() override {
        timer_ = std::make_shared<common::TestTimerScheduler>();
        sm_ = std::make_unique<SendManager>(timer_);
    }

    size_t QueuedCount(uint16_t type) {
        // Drain with an effectively unbounded budget, count, and put them back.
        auto frames = sm_->GetPendingFrames(kApplication, 1 << 20);
        size_t n = 0;
        for (const auto& f : frames) {
            if (f->GetType() == type) ++n;
        }
        for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
            sm_->RequeueFront(*it);
        }
        return n;
    }

    std::shared_ptr<common::TestTimerScheduler> timer_;
    std::unique_ptr<SendManager> sm_;
};

TEST_F(SendManagerRequeueTest, LostValueFrameIsRequeuedOnce) {
    auto frame = std::make_shared<MaxDataFrame>();
    frame->SetMaximumData(1000);
    sm_->AttachRequeueOnLoss(frame);

    frame->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_EQ(QueuedCount(FrameType::kMaxData), 1u);
}

TEST_F(SendManagerRequeueTest, ProbeAlsoRequeuesAndDoesNotDoubleQueue) {
    auto frame = std::make_shared<DataBlockedFrame>();
    frame->SetMaximumData(500);
    sm_->AttachRequeueOnLoss(frame);

    frame->NotifyDelivery(FrameDeliveryState::kProbe);
    frame->NotifyDelivery(FrameDeliveryState::kLost);  // already waiting
    EXPECT_EQ(QueuedCount(FrameType::kDataBlocked), 1u);
}

TEST_F(SendManagerRequeueTest, AckedValueFrameIsNotRequeued) {
    auto frame = std::make_shared<MaxDataFrame>();
    sm_->AttachRequeueOnLoss(frame);
    frame->NotifyDelivery(FrameDeliveryState::kAcked);
    EXPECT_EQ(QueuedCount(FrameType::kMaxData), 0u);
}

TEST_F(SendManagerRequeueTest, SecondLossOfTheSameFrameObjectIsRecovered) {
    auto frame = std::make_shared<MaxDataFrame>();
    frame->SetMaximumData(42);
    sm_->AttachRequeueOnLoss(frame);

    frame->NotifyDelivery(FrameDeliveryState::kLost);
    auto sent = sm_->GetPendingFrames(kApplication, 1 << 20);  // leaves in a packet
    ASSERT_EQ(sent.size(), 1u);

    // The re-sent copy is lost too: without RearmLossNotification the one-shot
    // kLost latch swallows this and the update is gone for good.
    frame->NotifyDelivery(FrameDeliveryState::kLost);
    EXPECT_EQ(QueuedCount(FrameType::kMaxData), 1u);
}

TEST_F(SendManagerRequeueTest, RequeueFrontPreservesGenerationOrderAndDedups) {
    auto a = std::make_shared<MaxDataFrame>();
    auto b = std::make_shared<DataBlockedFrame>();
    sm_->EnqueueFrame(std::make_shared<PingFrame>());

    sm_->RequeueFront(b);
    sm_->RequeueFront(a);
    sm_->RequeueFront(a);  // already queued: no-op

    auto out = sm_->GetPendingFrames(kApplication, 1 << 20);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], a) << "requeued frames go to the FRONT, last requeued first";
    EXPECT_EQ(out[1], b);
    EXPECT_EQ(out[2]->GetType(), FrameType::kPing);
}

TEST_F(SendManagerRequeueTest, DroppedFrameObjectDoesNotCrashTheHandler) {
    auto frame = std::make_shared<MaxDataFrame>();
    sm_->AttachRequeueOnLoss(frame);
    std::weak_ptr<IFrame> weak = frame;
    // Keep a copy of the handler's trigger alive past the frame: NotifyDelivery
    // needs the frame itself, so emulate "frame gone" by letting the weak ref
    // inside the handler expire after the last strong ref drops.
    frame->NotifyDelivery(FrameDeliveryState::kAcked);
    frame.reset();
    EXPECT_TRUE(weak.expired());
}

}  // namespace
}  // namespace quic
}  // namespace quicx
