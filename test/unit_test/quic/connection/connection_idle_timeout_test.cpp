#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "common/network/event_loop.h"

#include "quic/connection/connection_client.h"
#include "quic/connection/connection_state_machine.h"
#include "quic/connection/connection_timer_coordinator.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/packet/rtt_1_packet.h"

namespace quicx {
namespace quic {
namespace {

// Same recording seam as connection_base_close_behavior_test: watch the
// production event loop and record every timer scheduling call, so tests can
// assert "no new timer was armed" (a silent discard must not schedule a
// closing wait) or "the idle timer was re-armed" (a restart happened).
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

class TestIdleConnection: public ClientConnection {
public:
    using ClientConnection::ClientConnection;

    void ForceState(ConnectionStateType state) { state_machine_.SetState(state); }
    void TriggerIdleTimeoutForTest() { OnIdleTimeout(); }
    void ArmIdleTimerForTest() { timer_coordinator_->StartIdleTimer([]() {}); }
    // Direct injection: production populates max_idle_timeout via
    // Init(QuicTransportParams) during connection setup; tests use the same
    // path so the negotiated value (and the 3x PTO floor in
    // TimerCoordinator::GetIdleTimeoutMs) is exercised end-to-end.
    void EnableIdleTimeoutForTest(uint32_t ms) {
        QuicTransportParams conf;
        conf.max_idle_timeout_ms_ = ms;
        transport_param_.Init(conf);
    }
};

std::shared_ptr<TLSCtx> MakeTlsContext() {
    auto ctx = std::make_shared<TLSClientCtx>();
    ctx->Init(false);
    return ctx;
}

// ==================== EffectiveIdleTimeoutMs (RFC 9000 §10.1 floor) ====

TEST(EffectiveIdleTimeoutTest, ZeroMeansDisabled) {
    // A disabled timeout must stay disabled — the 3x PTO floor widens an
    // existing timeout but must never switch one on.
    EXPECT_EQ(EffectiveIdleTimeoutMs(0, 100), 0u);
    EXPECT_EQ(EffectiveIdleTimeoutMs(0, 0), 0u);
}

TEST(EffectiveIdleTimeoutTest, NegotiatedAboveFloorPassesThrough) {
    // 30s negotiated, 200ms PTO: 30s > 600ms floor, unchanged.
    EXPECT_EQ(EffectiveIdleTimeoutMs(30000, 200), 30000u);
}

TEST(EffectiveIdleTimeoutTest, NegotiatedBelowFloorIsRaised) {
    // Aggressively small 300ms negotiated with a 250ms PTO would close the
    // connection before a lost flight's first retransmission (PTO) could
    // even be answered. Raised to 3x PTO.
    EXPECT_EQ(EffectiveIdleTimeoutMs(300, 250), 750u);
    EXPECT_EQ(EffectiveIdleTimeoutMs(1, 250), 750u);
}

// ==================== silent close on idle expiry ====

// P0-C6: idle timeout discards the connection SILENTLY. The pre-fix code
// routed through InnerConnectionClose → graceful close → CONNECTION_CLOSE
// frame + 3x-PTO closing wait. The fix must reach kStateClosed with the
// close callback invoked and WITHOUT arming any new timer (no closing or
// draining wait — there is no peer left to wait out).
TEST(ConnectionIdleTimeoutTest, IdleTimeoutClosesSilentlyWithoutClosingTimer) {
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    bool close_callback_invoked = false;

    ConnectionCallbacks cbs;
    cbs.connection_close_cb = [&close_callback_invoked](std::shared_ptr<IConnection>, uint64_t, const std::string&) {
        close_callback_invoked = true;
    };
    auto conn = std::make_shared<TestIdleConnection>(MakeTlsContext(), event_loop, cbs);

    conn->ForceState(ConnectionStateType::kStateConnected);
    // Warm up: force-state transitions themselves schedule timers; snapshot
    // the recording AFTER that so the assertion below is unaffected.
    size_t delays_before = event_loop->delays().size();

    conn->TriggerIdleTimeoutForTest();

    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateClosed);
    EXPECT_TRUE(close_callback_invoked);
    // Silent discard: no closing/draining wait timer was scheduled. OnStateToClosed
    // only STOPS timers; a scheduled close wait here would prove we went
    // through the announce-close path instead.
    EXPECT_EQ(event_loop->delays().size(), delays_before);
}

TEST(ConnectionIdleTimeoutTest, IdleTimeoutFromConnectingIsAlsoSilent) {
    // A handshake that never completes hits the idle timer from Connecting;
    // it discards silently as well (and counts a handshake failure metric).
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    auto conn = std::make_shared<TestIdleConnection>(MakeTlsContext(), event_loop);

    conn->ForceState(ConnectionStateType::kStateConnecting);
    size_t delays_before = event_loop->delays().size();

    conn->TriggerIdleTimeoutForTest();

    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateClosed);
    EXPECT_EQ(event_loop->delays().size(), delays_before);
}

TEST(ConnectionIdleTimeoutTest, IdleTimeoutOnTerminatingStateIsNoop) {
    // A connection already in Closing/Draining has its own (stronger)
    // teardown semantics and its own timeout armed; a late idle expiry must
    // not preempt them.
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    auto conn = std::make_shared<TestIdleConnection>(MakeTlsContext(), event_loop);

    conn->ForceState(ConnectionStateType::kStateClosing);
    conn->TriggerIdleTimeoutForTest();
    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);

    conn->ForceState(ConnectionStateType::kStateDraining);
    conn->TriggerIdleTimeoutForTest();
    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateDraining);
}

// ==================== garbage datagrams must not keep connections alive ====

// P0-C6 (RFC 9000 §10.1): "An endpoint restarts its idle timer when a
// packet it receives is successfully processed". Pre-fix, ANY datagram —
// including undecryptable garbage — reset the timer, letting an attacker
// keep an abandoned connection alive forever with junk. This connection has
// no 1-RTT read keys installed (it was force-moved to Connected without a
// handshake), so every packet below fails decryption: the idle timer must
// NOT be re-armed (ResetIdleTimer cancels + re-adds, which would append to
// the recording).
TEST(ConnectionIdleTimeoutTest, GarbageDatagramDoesNotResetIdleTimer) {
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    auto conn = std::make_shared<TestIdleConnection>(MakeTlsContext(), event_loop);

    conn->ForceState(ConnectionStateType::kStateConnected);
    conn->EnableIdleTimeoutForTest(60000);
    conn->ArmIdleTimerForTest();
    ASSERT_FALSE(event_loop->delays().empty());
    size_t delays_before = event_loop->delays().size();

    // Undecryptable 1-RTT datagram: no read secrets installed, wrong DCID,
    // random payload — every plausible parse/decrypt path fails.
    std::vector<std::shared_ptr<IPacket>> garbage;
    for (int i = 0; i < 3; ++i) {
        auto pkt = std::make_shared<Rtt1Packet>();
        uint8_t bogus_cid[8] = {0xde, 0xad, 0xbe, 0xef, 0x11, 0x22, 0x33, 0x44};
        pkt->GetHeader()->SetDestinationConnectionId(bogus_cid, sizeof(bogus_cid));
        pkt->SetPacketNumber(100 + i);
        garbage.push_back(pkt);
    }
    uint32_t datagram_size = 0;
    for (const auto& p : garbage) {
        datagram_size += 64;  // nominal encoded size; only used for amplification accounting
    }
    conn->OnPackets(0, garbage, datagram_size);

    // No packet was successfully processed → no idle timer restart.
    EXPECT_EQ(event_loop->delays().size(), delays_before);

    // Sanity: the connection is untouched by the garbage (still Connected).
    EXPECT_EQ(conn->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
}

// P0-C6 wiring check: an aggressively small negotiated idle timeout must be
// floored by TimerCoordinator::GetIdleTimeoutMs at 3x PTO (default initial
// RTT gives PTO well above 100ms/3), observable in the armed timer delay.
TEST(ConnectionIdleTimeoutTest, ArmedIdleTimerRespectsThreePtoFloor) {
    auto event_loop = std::make_shared<RecordingEventLoop>();
    ASSERT_TRUE(event_loop->Init());
    auto conn = std::make_shared<TestIdleConnection>(MakeTlsContext(), event_loop);

    conn->ForceState(ConnectionStateType::kStateConnected);
    // 100ms negotiated would close the connection before even one PTO
    // retransmission round; the armed timer must exceed the raw value.
    conn->EnableIdleTimeoutForTest(100);
    conn->ArmIdleTimerForTest();

    ASSERT_FALSE(event_loop->delays().empty());
    EXPECT_GT(event_loop->delays().back(), 100u)
        << "3x PTO floor must raise a 100ms negotiated idle timeout";
}

}  // namespace
}  // namespace quic
}  // namespace quicx
