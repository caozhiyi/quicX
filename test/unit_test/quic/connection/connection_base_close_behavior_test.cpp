#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "common/network/event_loop.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/connection_closer.h"
#include "quic/connection/error.h"
#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {
namespace quic {
namespace {

// Records the delay of every timer the connection schedules, while still running
// the production engine underneath.
//
// This replaces a hand-rolled ITimer mock that was injected through
// SetTimerForTest(). Connection components now schedule through
// ITimerScheduler, which the event loop implements, so the seam moved from
// "swap the timer" to "watch the loop".
class RecordingEventLoop: public common::EventLoop {
public:
    common::Timer AddTimer(std::weak_ptr<void> owner, std::function<void()> cb, uint32_t delay_ms) override {
        delays_.push_back(delay_ms);
        return common::EventLoop::AddTimer(std::move(owner), std::move(cb), delay_ms);
    }

    void PostDelayed(std::function<void()> cb, uint32_t delay_ms) override {
        delays_.push_back(delay_ms);
        common::EventLoop::PostDelayed(std::move(cb), delay_ms);
    }

    const std::vector<uint32_t>& delays() const { return delays_; }

private:
    std::vector<uint32_t> delays_;
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
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    bool close_callback_invoked = false;

    ConnectionCallbacks cbs;
    cbs.connection_close_cb = [&close_callback_invoked](std::shared_ptr<IConnection>, uint64_t, const std::string&) {
        close_callback_invoked = true;
    };
    auto conn = std::make_shared<TestClientConnection>(MakeTlsContext(), event_loop, cbs);

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->Close();

    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    ASSERT_FALSE(event_loop->delays().empty());

    uint32_t close_wait = conn->GetCloseWaitTimeForTest();
    EXPECT_GE(close_wait, 500u);  // Minimum timeout enforced
    EXPECT_EQ(event_loop->delays().back(), close_wait * 3);
    // After weak_ptr refactoring, OnStateToClosing immediately invokes the
    // connection close callback so the application can release resources early.
    EXPECT_TRUE(close_callback_invoked);
}

TEST(ConnectionBaseCloseBehaviorTest, ImmediateCloseStoresErrorAndSchedulesTimer) {
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    auto conn = std::make_shared<TestClientConnection>(MakeTlsContext(), event_loop);

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->TriggerImmediateCloseForTest(0xdead, 0x15, "fatal");

    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    EXPECT_EQ(conn->GetStoredClosingError(), 0xdead);
    EXPECT_EQ(conn->GetStoredClosingTriggerFrame(), 0x15);
    EXPECT_EQ(conn->GetStoredClosingReason(), "fatal");

    ASSERT_FALSE(event_loop->delays().empty());
    uint32_t close_wait = conn->GetCloseWaitTimeForTest();
    EXPECT_EQ(event_loop->delays().back(), close_wait * 3);
}

TEST(ConnectionBaseCloseBehaviorTest, ClosingTimeoutInvokesCallback) {
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    uint64_t error_code = 0;
    std::string reason;

    ConnectionCallbacks cbs2;
    cbs2.connection_close_cb = [&error_code, &reason](
                                   std::shared_ptr<IConnection>, uint64_t err, const std::string& r) {
        error_code = err;
        reason = r;
    };
    auto conn = std::make_shared<TestClientConnection>(MakeTlsContext(), event_loop, cbs2);

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->Close();

    // Simulate timer firing
    conn->TriggerClosingTimeoutForTest();

    // After weak_ptr refactoring, the connection close callback fires immediately
    // in OnStateToClosing (before the timer). At that point the closing_reason_ is
    // empty because StartGracefulClose sets it to "". The subsequent OnStateToClosed
    // does NOT re-invoke the callback (guarded by connection_close_cb_invoked_).
    EXPECT_EQ(error_code, QuicErrorCode::kNoError);
    EXPECT_EQ(reason, "");
}

TEST(ConnectionBaseCloseBehaviorTest, CloseWaitTimeHasLowerBound) {
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    auto conn = std::make_shared<TestClientConnection>(MakeTlsContext(), event_loop);

    uint32_t close_wait = conn->GetCloseWaitTimeForTest();
    EXPECT_GE(close_wait, 500u);

    // Explicitly reset conn before event_loop goes out of scope so the
    // PathManager destructor's CleanupMigrationState() RunInLoop lambda
    // is enqueued and freed while the EventLoop is still alive.
    conn.reset();
}

}  // namespace
}  // namespace quic
}  // namespace quicx
