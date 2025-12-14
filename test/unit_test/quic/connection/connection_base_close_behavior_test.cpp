#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/error.h"
#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {
namespace quic {
namespace {

class RecordingTimer: public common::ITimer {
public:
    struct Entry {
        common::TimerTask task;
        uint32_t timeout_ms;
    };

    uint64_t AddTimer(common::TimerTask& task, uint32_t time, uint64_t /*now*/ = 0) override {
        entries_.push_back({task, time});
        return entries_.size();
    }

    bool RemoveTimer(common::TimerTask& /*task*/) override {
        ++rm_count_;
        return true;
    }

    int32_t MinTime(uint64_t /*now*/ = 0) override {
        return entries_.empty() ? -1 : static_cast<int32_t>(entries_.front().timeout_ms);
    }
    void TimerRun(uint64_t /*now*/ = 0) override {}
    bool Empty() override { return entries_.empty(); }

    size_t add_count() const { return entries_.size(); }
    size_t rm_count() const { return rm_count_; }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    size_t rm_count_{0};
};

class TestClientConnection: public ClientConnection {
public:
    using ClientConnection::ClientConnection;

    void ForceState(ConnectionStateType state) { state_machine_.SetState(state); }
    void TriggerIdleTimeoutForTest() { OnIdleTimeout(); }
    void TriggerClosingTimeoutForTest() { OnClosingTimeout(); }
    void TriggerImmediateCloseForTest(uint64_t error, uint16_t frame_type, const std::string& reason) {
        ImmediateClose(error, frame_type, reason);
    }
    uint32_t GetCloseWaitTimeForTest() { return connection_closer_->GetCloseWaitTime(); }
    uint64_t GetStoredClosingError() const { return connection_closer_->GetClosingErrorCode(); }
    uint16_t GetStoredClosingTriggerFrame() const { return connection_closer_->GetClosingTriggerFrame(); }
    std::string GetStoredClosingReason() const { return connection_closer_->GetClosingReason(); }
};

std::shared_ptr<TLSCtx> MakeTlsContext() {
    auto ctx = std::make_shared<TLSClientCtx>();
    ctx->Init(false);
    return ctx;
}

TEST(ConnectionBaseCloseBehaviorTest, CloseSchedulesThreePtoTimer) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto timer = std::make_shared<RecordingTimer>();
    event_loop->SetTimerForTest(timer);
    bool close_callback_invoked = false;

    auto conn =
        std::make_shared<TestClientConnection>(MakeTlsContext(), ISender::MakeSender(), event_loop, nullptr, nullptr,
            nullptr, nullptr, [&close_callback_invoked](std::shared_ptr<IConnection>, uint64_t, const std::string&) {
                close_callback_invoked = true;
            });

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->Close();

    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    ASSERT_FALSE(timer->entries().empty());

    uint32_t close_wait = conn->GetCloseWaitTimeForTest();
    EXPECT_GE(close_wait, 500u);  // Minimum timeout enforced
    EXPECT_EQ(timer->entries().back().timeout_ms, close_wait * 3);
    EXPECT_FALSE(close_callback_invoked);
}

TEST(ConnectionBaseCloseBehaviorTest, ImmediateCloseStoresErrorAndSchedulesTimer) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto timer = std::make_shared<RecordingTimer>();
    event_loop->SetTimerForTest(timer);
    auto conn = std::make_shared<TestClientConnection>(
        MakeTlsContext(), ISender::MakeSender(), event_loop, nullptr, nullptr, nullptr, nullptr, nullptr);

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->TriggerImmediateCloseForTest(0xdead, 0x15, "fatal");

    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    EXPECT_EQ(conn->GetStoredClosingError(), 0xdead);
    EXPECT_EQ(conn->GetStoredClosingTriggerFrame(), 0x15);
    EXPECT_EQ(conn->GetStoredClosingReason(), "fatal");

    ASSERT_FALSE(timer->entries().empty());
    uint32_t close_wait = conn->GetCloseWaitTimeForTest();
    EXPECT_EQ(timer->entries().back().timeout_ms, close_wait * 3);
}

TEST(ConnectionBaseCloseBehaviorTest, ClosingTimeoutInvokesCallback) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto timer = std::make_shared<RecordingTimer>();
    event_loop->SetTimerForTest(timer);
    uint64_t error_code = 0;
    std::string reason;

    auto conn =
        std::make_shared<TestClientConnection>(MakeTlsContext(), ISender::MakeSender(), event_loop, nullptr, nullptr,
            nullptr, nullptr, [&error_code, &reason](std::shared_ptr<IConnection>, uint64_t err, const std::string& r) {
                error_code = err;
                reason = r;
            });

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->Close();

    // Simulate timer firing
    conn->TriggerClosingTimeoutForTest();

    EXPECT_EQ(error_code, QuicErrorCode::kNoError);
    EXPECT_EQ(reason, "normal close.");
}

TEST(ConnectionBaseCloseBehaviorTest, CloseWaitTimeHasLowerBound) {
    auto event_loop = common::MakeEventLoop();
    auto timer = std::make_shared<RecordingTimer>();
    event_loop->SetTimerForTest(timer);
    auto conn = std::make_shared<TestClientConnection>(
        MakeTlsContext(), ISender::MakeSender(), event_loop, nullptr, nullptr, nullptr, nullptr, nullptr);

    uint32_t close_wait = conn->GetCloseWaitTimeForTest();
    EXPECT_GE(close_wait, 500u);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
