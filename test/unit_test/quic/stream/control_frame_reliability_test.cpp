#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "quic/frame/max_stream_data_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"

// P0-C10 / INV1 reliability sweep: with the whole-packet retransmission
// channel retired, every ack-eliciting stream-level control frame must have an
// owner that re-queues it on kLost / kProbe -- and a re-sent frame must get a
// fresh loss budget so a SECOND loss is not swallowed by the one-shot latch.

namespace quicx {
namespace quic {
namespace {

class CtlVisitor: public IFrameVisitor {
public:
    bool HandleFrame(std::shared_ptr<IFrame> frame) override {
        frames_.push_back(frame);
        return true;
    }
    std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
    uint8_t GetEncryptionLevel() override { return kApplication; }
    void SetStreamDataSizeLimit(uint32_t size) override { allowance_ = size; }
    uint32_t GetLeftStreamDataSize() override { return allowance_; }
    uint64_t GetStreamDataSize() override { return 0; }
    uint32_t GetPacketLeftSize() override { return 1400; }

    template <typename T>
    std::shared_ptr<T> First() const {
        for (const auto& f : frames_) {
            if (auto t = std::dynamic_pointer_cast<T>(f)) return t;
        }
        return nullptr;
    }
    template <typename T>
    size_t Count() const {
        size_t n = 0;
        for (const auto& f : frames_) {
            if (std::dynamic_pointer_cast<T>(f)) ++n;
        }
        return n;
    }

    std::vector<std::shared_ptr<IFrame>> frames_;
    uint32_t allowance_{100000};
};

class ControlFrameReliabilityTest: public ::testing::Test {
protected:
    void SetUp() override {
        loop_ = common::MakeEventLoop();
        ASSERT_TRUE(loop_->Init());
    }
    std::shared_ptr<SendStream> MakeSend(uint64_t limit = 1000000) {
        return std::make_shared<SendStream>(
            loop_, limit, 4, [](std::shared_ptr<IStream>) {}, [](uint64_t) {},
            [](uint64_t, uint16_t, const std::string&) {});
    }
    std::shared_ptr<common::IEventLoop> loop_;
};

TEST_F(ControlFrameReliabilityTest, ResetStreamIsRequeuedOnLossAndOnProbe) {
    auto stream = MakeSend();
    stream->Reset(7);

    CtlVisitor first;
    stream->TrySendData(&first);
    auto reset = first.First<ResetStreamFrame>();
    ASSERT_NE(reset, nullptr);
    ASSERT_TRUE(reset->HasDeliveryHandler()) << "RESET_STREAM must have an owner";

    reset->NotifyDelivery(FrameDeliveryState::kLost);
    CtlVisitor second;
    stream->TrySendData(&second);
    EXPECT_EQ(second.Count<ResetStreamFrame>(), 1u) << "a lost RESET_STREAM must be re-sent";
    EXPECT_EQ(second.First<ResetStreamFrame>()->GetAppErrorCode(), 7u) << "same abort, same error code";

    // kProbe also re-sends (PTO prefers retransmittable data).
    reset->NotifyDelivery(FrameDeliveryState::kProbe);
    CtlVisitor third;
    stream->TrySendData(&third);
    EXPECT_EQ(third.Count<ResetStreamFrame>(), 1u);
}

TEST_F(ControlFrameReliabilityTest, SecondLossOfTheSameFrameIsNotSwallowedByTheLatch) {
    auto stream = MakeSend();
    stream->Reset(9);

    CtlVisitor v0;
    stream->TrySendData(&v0);
    auto reset = v0.First<ResetStreamFrame>();
    ASSERT_NE(reset, nullptr);

    // Generation 1: lost -> re-queued -> re-sent (same frame object).
    reset->NotifyDelivery(FrameDeliveryState::kLost);
    CtlVisitor v1;
    stream->TrySendData(&v1);
    ASSERT_EQ(v1.Count<ResetStreamFrame>(), 1u);

    // Generation 2: the re-sent copy is lost AGAIN. Before RearmLossNotification
    // the kLost latch (once per instance) silently dropped this, and with the
    // whole-packet channel gone the abort would never reach the peer.
    reset->NotifyDelivery(FrameDeliveryState::kLost);
    CtlVisitor v2;
    stream->TrySendData(&v2);
    EXPECT_EQ(v2.Count<ResetStreamFrame>(), 1u) << "a second loss must be recovered too";
}

TEST_F(ControlFrameReliabilityTest, QueuedFrameIsNotDuplicatedByProbeThenLost) {
    auto stream = MakeSend();
    stream->Reset(1);
    CtlVisitor v0;
    stream->TrySendData(&v0);
    auto reset = v0.First<ResetStreamFrame>();
    ASSERT_NE(reset, nullptr);

    reset->NotifyDelivery(FrameDeliveryState::kProbe);
    reset->NotifyDelivery(FrameDeliveryState::kLost);  // already waiting: no 2nd copy

    CtlVisitor v1;
    stream->TrySendData(&v1);
    EXPECT_EQ(v1.Count<ResetStreamFrame>(), 1u);
}

TEST_F(ControlFrameReliabilityTest, AckedControlFrameIsNeverResent) {
    auto stream = MakeSend();
    stream->Reset(3);
    CtlVisitor v0;
    stream->TrySendData(&v0);
    auto reset = v0.First<ResetStreamFrame>();
    ASSERT_NE(reset, nullptr);

    reset->NotifyDelivery(FrameDeliveryState::kAcked);
    CtlVisitor v1;
    stream->TrySendData(&v1);
    EXPECT_EQ(v1.Count<ResetStreamFrame>(), 0u);
}

TEST_F(ControlFrameReliabilityTest, StreamDataBlockedIsResentOnlyWhileTheReportedLimitIsCurrent) {
    // Peer limit 100, 90 bytes already sent -> slack below the threshold, so
    // STREAM_DATA_BLOCKED is emitted at limit 100.
    auto stream = MakeSend(100);
    std::vector<uint8_t> bytes(90, 1);
    ASSERT_EQ(stream->Send(bytes.data(), 90), 90);
    CtlVisitor v0;
    stream->TrySendData(&v0);
    auto blocked = v0.First<StreamDataBlockedFrame>();
    ASSERT_NE(blocked, nullptr) << "scenario must produce a STREAM_DATA_BLOCKED";
    ASSERT_TRUE(blocked->HasDeliveryHandler());

    // Limit unchanged: a lost BLOCKED is re-sent (the producer de-duplicates
    // per limit and would never mint it again).
    blocked->NotifyDelivery(FrameDeliveryState::kLost);
    CtlVisitor v1;
    stream->TrySendData(&v1);
    EXPECT_EQ(v1.Count<StreamDataBlockedFrame>(), 1u);

    // Peer raised the limit: the old BLOCKED is obsolete and must NOT be re-sent.
    auto max_frame = std::make_shared<MaxStreamDataFrame>();
    max_frame->SetStreamID(4);
    max_frame->SetMaximumData(100000);
    stream->OnFrame(max_frame);
    blocked->RearmLossNotification();
    blocked->NotifyDelivery(FrameDeliveryState::kLost);
    CtlVisitor v2;
    stream->TrySendData(&v2);
    EXPECT_EQ(v2.Count<StreamDataBlockedFrame>(), 0u) << "stale BLOCKED (limit moved on) must not be re-sent";
}

TEST_F(ControlFrameReliabilityTest, HandlerDoesNotOutliveItsStream) {
    // The frame sits in the unacked table long after the stream may be gone;
    // the handler holds only weak references and must not touch freed memory.
    auto stream = MakeSend();
    stream->Reset(5);
    CtlVisitor v0;
    stream->TrySendData(&v0);
    auto reset = v0.First<ResetStreamFrame>();
    ASSERT_NE(reset, nullptr);

    stream.reset();
    reset->NotifyDelivery(FrameDeliveryState::kLost);   // must be a safe no-op
    reset->NotifyDelivery(FrameDeliveryState::kProbe);
    SUCCEED();
}

TEST_F(ControlFrameReliabilityTest, StopSendingIsRequeuedOnLoss) {
    auto recv = std::make_shared<RecvStream>(
        loop_, 100000, 8, [](std::shared_ptr<IStream>) {}, [](uint64_t) {},
        [](uint64_t, uint16_t, const std::string&) {});
    recv->Reset(11);  // locally abandon the stream -> STOP_SENDING

    CtlVisitor v0;
    recv->TrySendData(&v0);
    auto stop = v0.First<StopSendingFrame>();
    ASSERT_NE(stop, nullptr);
    ASSERT_TRUE(stop->HasDeliveryHandler()) << "STOP_SENDING must have an owner";

    stop->NotifyDelivery(FrameDeliveryState::kLost);
    CtlVisitor v1;
    recv->TrySendData(&v1);
    EXPECT_EQ(v1.Count<StopSendingFrame>(), 1u);
    EXPECT_EQ(v1.First<StopSendingFrame>()->GetAppErrorCode(), 11u);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
