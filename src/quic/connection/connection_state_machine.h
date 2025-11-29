#ifndef QUIC_CONNECTION_CONNECTION_STATE_MACHINE
#define QUIC_CONNECTION_CONNECTION_STATE_MACHINE

#include <cstdint>
#include <functional>
#include <string>
#include "common/log/log.h"

namespace quicx {
namespace quic {

enum class ConnectionStateType {
    kStateConnecting,
    kStateConnected,
    kStateClosing,   // Initiated connection close, sent CONNECTION_CLOSE, waiting 1×PTO
    kStateDraining,  // Received CONNECTION_CLOSE from peer, no packets sent, waiting 3×PTO
    kStateClosed,
};

class IConnectionStateListener {
public:
    virtual ~IConnectionStateListener() = default;
    virtual void OnStateToConnecting() = 0;
    virtual void OnStateToConnected() = 0;
    virtual void OnStateToClosing() = 0;
    virtual void OnStateToDraining() = 0;
    virtual void OnStateToClosed() = 0;
};

class ConnectionStateMachine {
public:
    ConnectionStateMachine(IConnectionStateListener* listener);
    ~ConnectionStateMachine() = default;

    // State transitions
    void OnHandshakeDone();
    void OnClose();                         // Local close
    void OnConnectionCloseFrameReceived();  // Peer close
    void OnCloseTimeout();                  // Timeout for closing/draining

    // State checks
    bool AllowSend() const;
    bool AllowReceive() const;  // Should we process received packets?

    ConnectionStateType GetState() const { return state_; }

    void SetState(ConnectionStateType new_state);
    std::string StateToString(ConnectionStateType state) const;

private:
    ConnectionStateType state_;
    IConnectionStateListener* listener_;
};

}  // namespace quic
}  // namespace quicx

#endif
