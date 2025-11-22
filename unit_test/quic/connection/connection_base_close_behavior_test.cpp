#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/error.h"
#include "quic/quicx/global_resource.h"
#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {
namespace quic {
namespace {

class RecordingTimer : public common::ITimer {
public:
    struct Entry {
        common::TimerTask task;
        uint32_t timeout_ms;
    };

    uint64_t AddTimer(common::TimerTask& task, uint32_t time, uint64_t /*now*/ = 0) override {
        entries_.push_back({task, time});
        return entries_.size();
    }

    bool RmTimer(common::TimerTask& /*task*/) override {
        ++rm_count_;
        return true;
    }

    int32_t MinTime(uint64_t /*now*/ = 0) override { return entries_.empty() ? -1 : static_cast<int32_t>(entries_.front().timeout_ms); }
    void TimerRun(uint64_t /*now*/ = 0) override {}
    bool Empty() override { return entries_.empty(); }

    size_t add_count() const { return entries_.size(); }
    size_t rm_count() const { return rm_count_; }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
    size_t rm_count_ {0};
};

class TestClientConnection : public ClientConnection {
public:
    using ClientConnection::ClientConnection;

    void ForceState(ConnectionStateType state) { state_ = state; }
    void TriggerIdleTimeoutForTest() { OnIdleTimeout(); }
    void TriggerClosingTimeoutForTest() { OnClosingTimeout(); }
    void TriggerImmediateCloseForTest(uint64_t error, uint16_t frame_type, const std::string& reason) {
        ImmediateClose(error, frame_type, reason);
    }
    uint32_t GetCloseWaitTimeForTest() { return GetCloseWaitTime(); }
    uint64_t GetStoredClosingError() const { return closing_error_code_; }
    uint16_t GetStoredClosingTriggerFrame() const { return closing_trigger_frame_; }
    std::string GetStoredClosingReason() const { return closing_reason_; }
};

std::shared_ptr<TLSCtx> MakeTlsContext() {
    auto ctx = std::make_shared<TLSClientCtx>();
    ctx->Init(false);
    return ctx;
}

TEST(ConnectionBaseCloseBehaviorTest, CloseSchedulesThreePtoTimer) {
    auto timer = std::make_shared<RecordingTimer>();
    GlobalResource::Instance().GetThreadLocalEventLoop()->SetTimerForTest(timer);
    bool close_callback_invoked = false;

    auto conn = std::make_shared<TestClientConnection>(
        MakeTlsContext(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        [&close_callback_invoked](std::shared_ptr<IConnection>, uint64_t, const std::string&) {
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
    GlobalResource::Instance().ResetForTest();
}

TEST(ConnectionBaseCloseBehaviorTest, ImmediateCloseStoresErrorAndSchedulesTimer) {
    auto timer = std::make_shared<RecordingTimer>();
    GlobalResource::Instance().GetThreadLocalEventLoop()->SetTimerForTest(timer);
    auto conn = std::make_shared<TestClientConnection>(
        MakeTlsContext(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->TriggerImmediateCloseForTest(0xdead, 0x15, "fatal");

    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    EXPECT_EQ(conn->GetStoredClosingError(), 0xdead);
    EXPECT_EQ(conn->GetStoredClosingTriggerFrame(), 0x15);
    EXPECT_EQ(conn->GetStoredClosingReason(), "fatal");

    ASSERT_FALSE(timer->entries().empty());
    uint32_t close_wait = conn->GetCloseWaitTimeForTest();
    EXPECT_EQ(timer->entries().back().timeout_ms, close_wait * 3);
    GlobalResource::Instance().ResetForTest();
}

TEST(ConnectionBaseCloseBehaviorTest, ClosingTimeoutInvokesCallback) {
    auto timer = std::make_shared<RecordingTimer>();
    GlobalResource::Instance().GetThreadLocalEventLoop()->SetTimerForTest(timer);
    uint64_t error_code = 0;
    std::string reason;

    auto conn = std::make_shared<TestClientConnection>(
        MakeTlsContext(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        [&error_code, &reason](std::shared_ptr<IConnection>, uint64_t err, const std::string& r) {
            error_code = err;
            reason = r;
        });

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->Close();

    // Simulate timer firing
    conn->TriggerClosingTimeoutForTest();

    EXPECT_EQ(error_code, QuicErrorCode::kNoError);
    EXPECT_EQ(reason, "normal close.");
    GlobalResource::Instance().ResetForTest();
}

TEST(ConnectionBaseCloseBehaviorTest, CloseWaitTimeHasLowerBound) {
    auto timer = std::make_shared<RecordingTimer>();
    GlobalResource::Instance().GetThreadLocalEventLoop()->SetTimerForTest(timer);
    auto conn = std::make_shared<TestClientConnection>(
        MakeTlsContext(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    uint32_t close_wait = conn->GetCloseWaitTimeForTest();
    EXPECT_GE(close_wait, 500u);
    GlobalResource::Instance().ResetForTest();
}

}  // namespace
}  // namespace quic
}  // namespace quicx
