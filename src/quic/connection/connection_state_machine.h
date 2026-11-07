#ifndef QUIC_CONNECTION_CONNECTION_STATE_MACHINE
#define QUIC_CONNECTION_CONNECTION_STATE_MACHINE

#include <string>

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
    // Idle-timeout discard. RFC 9000 §10.1: the connection is torn down
    // SILENTLY — no CONNECTION_CLOSE frame, no closing/draining wait. When
    // the idle timer fires the peer has failed both restart conditions
    // (no processed packets, no ack-eliciting sends) for the whole
    // negotiated window, so there is no one left to notify. Already-
    // terminating states keep their own shutdown path.
    void OnSilentClose();

    // State checks
    bool AllowSend() const;
    bool AllowReceive() const;  // Should we process received packets?

    ConnectionStateType GetState() const { return state_; }

    // Query methods for cleaner state checks (Phase 3 optimization)
    bool CanSendData() const;
    bool CanReceiveData() const;
    bool IsClosing() const;
    bool IsDraining() const;
    bool IsClosed() const;
    bool IsTerminating() const;        // Closing || Draining || Closed
    bool ShouldIgnorePackets() const;  // Draining || Closed

    void SetState(ConnectionStateType new_state);
    std::string StateToString(ConnectionStateType state) const;

private:
    ConnectionStateType state_;
    IConnectionStateListener* listener_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONNECTION_STATE_MACHINE
