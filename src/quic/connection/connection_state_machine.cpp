#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

#include "quic/connection/connection_state_machine.h"

namespace quicx {
namespace quic {

ConnectionStateMachine::ConnectionStateMachine(IConnectionStateListener* listener):
    state_(ConnectionStateType::kStateConnecting),
    listener_(listener) {}

void ConnectionStateMachine::OnHandshakeDone() {
    if (state_ == ConnectionStateType::kStateConnecting) {
        SetState(ConnectionStateType::kStateConnected);

        // Metrics: Handshake success
        common::Metrics::CounterInc(common::MetricsStd::QuicHandshakeSuccess);
    }
}

void ConnectionStateMachine::OnClose() {
    if (state_ == ConnectionStateType::kStateConnected || state_ == ConnectionStateType::kStateConnecting) {
        // Metrics: Handshake failed if closing from connecting state
        if (state_ == ConnectionStateType::kStateConnecting) {
            common::Metrics::CounterInc(common::MetricsStd::QuicHandshakeFail);
        }

        SetState(ConnectionStateType::kStateClosing);
    }
}

void ConnectionStateMachine::OnConnectionCloseFrameReceived() {
    if (state_ != ConnectionStateType::kStateClosed && state_ != ConnectionStateType::kStateDraining) {
        SetState(ConnectionStateType::kStateDraining);
    }
}

void ConnectionStateMachine::OnCloseTimeout() {
    if (state_ == ConnectionStateType::kStateClosing || state_ == ConnectionStateType::kStateDraining) {
        SetState(ConnectionStateType::kStateClosed);
    }
}

bool ConnectionStateMachine::AllowSend() const {
    return state_ == ConnectionStateType::kStateConnecting || state_ == ConnectionStateType::kStateConnected;
}

bool ConnectionStateMachine::AllowReceive() const {
    return state_ != ConnectionStateType::kStateClosed;
}

void ConnectionStateMachine::SetState(ConnectionStateType new_state) {
    if (state_ == new_state) {
        return;
    }
    ConnectionStateType old_state = state_;
    state_ = new_state;
    common::LOG_INFO(
        "Connection state changed from %s to %s", StateToString(old_state).c_str(), StateToString(new_state).c_str());

    if (listener_) {
        switch (new_state) {
            case ConnectionStateType::kStateConnecting:
                listener_->OnStateToConnecting();
                break;
            case ConnectionStateType::kStateConnected:
                listener_->OnStateToConnected();
                break;
            case ConnectionStateType::kStateClosing:
                listener_->OnStateToClosing();
                break;
            case ConnectionStateType::kStateDraining:
                listener_->OnStateToDraining();
                break;
            case ConnectionStateType::kStateClosed:
                listener_->OnStateToClosed();
                break;
        }
    }
}

std::string ConnectionStateMachine::StateToString(ConnectionStateType state) const {
    switch (state) {
        case ConnectionStateType::kStateConnecting:
            return "Connecting";
        case ConnectionStateType::kStateConnected:
            return "Connected";
        case ConnectionStateType::kStateClosing:
            return "Closing";
        case ConnectionStateType::kStateDraining:
            return "Draining";
        case ConnectionStateType::kStateClosed:
            return "Closed";
        default:
            return "Unknown";
    }
}

}  // namespace quic
}  // namespace quicx
