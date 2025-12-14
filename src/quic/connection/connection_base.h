#ifndef QUIC_CONNECTION_CONNECTION_BASE
#define QUIC_CONNECTION_CONNECTION_BASE

#include <cstdint>
#include <memory>
#include <vector>

#include "common/network/if_event_loop.h"

#include "quic/connection/connection_closer.h"
#include "quic/connection/connection_crypto.h"
#include "quic/connection/connection_frame_processor.h"
#include "quic/connection/connection_id_coordinator.h"
#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_path_manager.h"
#include "quic/connection/connection_state_machine.h"
#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/connection_timer_coordinator.h"
#include "quic/connection/controler/connection_flow_control.h"
#include "quic/connection/controler/recv_control.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/if_connection.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/type.h"
#include "quic/include/type.h"

namespace quicx {
namespace quic {

class BaseConnection:
    public IConnection,
    public IConnectionStateListener,
    public std::enable_shared_from_this<BaseConnection> {
public:
    BaseConnection(StreamIDGenerator::StreamStarter start, bool ecn_enabled, std::shared_ptr<common::IEventLoop> loop,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(ConnectionID&)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)>
            connection_close_cb);

    // RFC 9000: Callback for immediate packet sending (bypasses normal flow)
    using ImmediateSendCallback = std::function<void(std::shared_ptr<common::IBuffer>, const common::Address&)>;
    void SetImmediateSendCallback(ImmediateSendCallback cb);

    virtual ~BaseConnection();
    //*************** outside interface ***************//
    virtual void Close() override;
    void SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> cb);
    virtual void Reset(uint32_t error_code) override;
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type) override;
    virtual bool MakeStreamAsync(StreamDirection type, stream_creation_callback callback) override;
    virtual uint64_t AddTimer(timer_callback callback, uint32_t timeout_ms) override;
    virtual void RemoveTimer(uint64_t timer_id) override;

    // *************** inner interface ***************//
    // set transport param
    void AddTransportParam(const QuicTransportParams& tp_config) override;
    virtual uint64_t GetConnectionIDHash() override;
    // try to build a quic message
    virtual bool GenerateSendData(std::shared_ptr<common::IBuffer> buffer, SendOperation& send_operation) override;
    // handle packets
    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) override;
    virtual void SetPendingEcn(uint8_t ecn) override { pending_ecn_ = ecn; }
    virtual EncryptionLevel GetCurEncryptionLevel() override;

    // observed peer address from network; store as candidate if different
    virtual void OnObservedPeerAddress(const common::Address& addr) override;

    // Test-only helper to expose cryptographer for decoding in unit tests
    virtual std::shared_ptr<ICryptographer> GetCryptographerForTest(uint16_t level) override {
        return connection_crypto_.GetCryptographer(level);
    }

    // Test-only helper to check remote CID manager state
    std::shared_ptr<ConnectionIDManager> GetRemoteConnectionIDManagerForTest() {
        return cid_coordinator_->GetRemoteConnectionIDManagerForTest();
    }

    // Get all local CID hashes for this connection (for cleanup on close)
    virtual std::vector<uint64_t> GetAllLocalCIDHashes() override { return cid_coordinator_->GetAllLocalCIDHashes(); }

    // Test-only interface to observe connection state
    ConnectionStateType GetConnectionStateForTest() const { return state_machine_.GetState(); }

    std::shared_ptr<common::IEventLoop> GetEventLoop() { return event_loop_; }

    // Get qlog trace for this connection
    std::shared_ptr<common::QlogTrace> GetQlogTrace() const { return qlog_trace_; }

    // IConnectionStateListener
    virtual void OnStateToConnecting() override {}
    virtual void OnStateToConnected() override;
    virtual void OnStateToClosing() override;
    virtual void OnStateToDraining() override;
    virtual void OnStateToClosed() override;

protected:
    bool OnInitialPacket(std::shared_ptr<IPacket> packet);
    bool On0rttPacket(std::shared_ptr<IPacket> packet);
    bool On1rttPacket(std::shared_ptr<IPacket> packet);
    bool OnNormalPacket(std::shared_ptr<IPacket> packet);
    bool OnVersionNegotiationPacket(std::shared_ptr<IPacket> packet);
    virtual bool OnHandshakePacket(std::shared_ptr<IPacket> packet);
    virtual bool OnRetryPacket(std::shared_ptr<IPacket> packet) = 0;

    // handle frames (delegated to frame processor)
    bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level);

    // Send ACK packet immediately at specified encryption level
    // Used when immediate ACK is required but current encryption level differs
    bool SendImmediateAckAtLevel(PacketNumberSpace ns);

    void OnTransportParams(TransportParam& remote_tp);

protected:
    virtual void ThreadTransferBefore() override;
    virtual void ThreadTransferAfter() override;
    // idle timeout
    void OnIdleTimeout();
    void OnClosingTimeout();
    void CheckPTOTimeout();  // RFC 9002: Check for idle timeout from excessive PTOs

    void ToSendFrame(std::shared_ptr<IFrame> frame);
    void ActiveSendStream(std::shared_ptr<IStream> stream);
    void ActiveSend();

    void InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string reason);
    void ImmediateClose(uint64_t error, uint16_t tigger_frame, std::string reason);
    void InnerStreamClose(uint64_t stream_id);

    // Stream data ACK notification callback
    void OnStreamDataAcked(uint64_t stream_id, uint64_t max_offset, bool has_fin);

    // Retry pending stream creation requests after receiving MAX_STREAMS
    void RetryPendingStreamRequests();

    void AddConnectionId(ConnectionID& id);
    void RetireConnectionId(ConnectionID& id);

    virtual void WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err) = 0;

    // record bytes received on candidate path to increase amp budget while probing
    virtual void OnCandidatePathDatagramReceived(const common::Address& addr, uint32_t bytes) override {
        if (path_manager_ && path_manager_->IsPathProbeInflight() &&
            (addr == path_manager_->GetCandidatePeerAddress())) {
            path_manager_->OnCandidatePathBytesReceived(bytes);
        }
    }
    virtual common::Address AcquireSendAddress() override {
        if (path_manager_) {
            return path_manager_->GetSendAddress();
        }
        return peer_addr_;
    }

    void CloseInternal();

    // Connection ID pool management
    void CheckAndReplenishLocalCIDPool();

protected:
    // transport param verify done
    TransportParam transport_param_;
    // timer coordinator (refactored from direct timer operations)
    std::unique_ptr<TimerCoordinator> timer_coordinator_;
    // connection ID coordinator (refactored from direct CID management)
    std::unique_ptr<ConnectionIDCoordinator> cid_coordinator_;
    // path manager (refactored from direct path management)
    std::unique_ptr<PathManager> path_manager_;
    // stream manager (refactored from direct stream management)
    std::unique_ptr<StreamManager> stream_manager_;
    // connection closer (refactored from direct close logic)
    std::unique_ptr<ConnectionCloser> connection_closer_;
    // frame processor (refactored from direct frame handling)
    std::unique_ptr<FrameProcessor> frame_processor_;
    // last time communicate, use to idle shutdown
    uint64_t last_communicate_time_;

    uint8_t pending_ecn_{0};
    bool ecn_enabled_;
    // flow control
    ConnectionFlowControl flow_control_;
    RecvControl recv_control_;
    SendManager send_manager_;
    // crypto
    ConnectionCrypto connection_crypto_;
    // token
    std::string token_;
    std::shared_ptr<TLSConnection> tls_connection_;

    ConnectionStateMachine state_machine_;

    // Qlog trace for this connection
    std::shared_ptr<common::QlogTrace> qlog_trace_;

    // hint for early-data scheduling: whether any application stream (id != 0) has pending send
    bool has_app_send_pending_ = false;
    // Track whether Initial packet has been sent in 0-RTT scenarios
    bool initial_packet_sent_ = false;

    // EventLoop reference for safe cleanup in destructor
    std::shared_ptr<common::IEventLoop> event_loop_;

    // Immediate send callback for bypassing normal send flow (e.g., immediate ACK)
    ImmediateSendCallback immediate_send_cb_;

    // Metrics: Handshake timing
    uint64_t handshake_start_time_{0};
};

}  // namespace quic
}  // namespace quicx

#endif