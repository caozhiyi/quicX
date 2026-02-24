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
#include "quic/connection/controler/recv_control.h"
#include "quic/connection/controler/recv_flow_controller.h"
#include "quic/connection/controler/send_flow_controller.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/encryption_level_scheduler.h"
#include "quic/connection/if_connection.h"
#include "quic/connection/if_connection_event_sink.h"
#include "quic/connection/key_update_trigger.h"
#include "quic/connection/transport_param.h"
#include "quic/include/type.h"
#include "quic/udp/if_sender.h"

namespace quicx {
namespace quic {

class BaseConnection:
    public IConnection,
    public IConnectionStateListener,
    public IConnectionEventSink,
    public std::enable_shared_from_this<BaseConnection> {
public:
    BaseConnection(StreamIDGenerator::StreamStarter start, bool ecn_enabled, std::shared_ptr<common::IEventLoop> loop,
        std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
        std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
        std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(ConnectionID&)> retire_conn_id_cb,
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)>
            connection_close_cb);
    // Set sender for direct packet transmission
    void SetSender(std::shared_ptr<ISender> sender) override;

    virtual ~BaseConnection();
    //*************** outside interface ***************//
    virtual void Close() override;
    void SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> cb);
    virtual void Reset(uint32_t error_code) override;
    virtual std::shared_ptr<IQuicStream> MakeStream(StreamDirection type) override;
    virtual bool MakeStreamAsync(StreamDirection type, stream_creation_callback callback) override;
    // Override to also update FrameProcessor's callback
    virtual void SetStreamStateCallBack(stream_state_callback cb) override;
    virtual uint64_t AddTimer(timer_callback callback, uint32_t timeout_ms) override;
    virtual void RemoveTimer(uint64_t timer_id) override;
    virtual bool IsTerminating() const override;

    // RFC 9001 Section 6: Key Update support
    void SetKeyUpdateEnabled(bool enabled) { key_update_trigger_.SetEnabled(enabled); }
    bool TriggerKeyUpdate() { return connection_crypto_.TriggerKeyUpdate(); }

    // RFC 9369: QUIC Version management
    void SetVersion(uint32_t version) {
        quic_version_ = version;
        connection_crypto_.SetVersion(version);
    }
    uint32_t GetVersion() const { return quic_version_; }

    // RFC 9000 Section 6: Version Negotiation
    typedef std::function<void(uint32_t new_version)> version_negotiation_callback;
    void SetVersionNegotiationCallback(version_negotiation_callback cb) { version_negotiation_cb_ = cb; }
    void SetVersionNegotiationDone() { version_negotiation_done_ = true; }
    bool IsVersionNegotiationNeeded() const { return version_negotiation_needed_; }
    uint32_t GetNegotiatedVersion() const { return negotiated_version_; }

    // *************** inner interface ***************//
    // set transport param
    void AddTransportParam(const QuicTransportParams& tp_config) override;
    virtual uint64_t GetConnectionIDHash() override;

    // ==================== New High-Level Send Interfaces ====================

    /**
     * @brief Main send interface (replaces GenerateSendData)
     *
     * Called by Worker to attempt sending data. Internally decides whether to send,
     * what to send, and handles all packet building and transmission.
     *
     * @return true if successfully sent data, false if no data or send failed
     */
    bool TrySend() override;

    /**
     * @brief Send ACK packet immediately
     *
     * Simplified interface for immediate ACK sending, used for cross-level ACKs
     * or when immediate ACK is required.
     *
     * @param ns Packet number space
     * @return true if successfully sent
     */
    bool SendImmediateAck(PacketNumberSpace ns);

    /**
     * @brief Send single frame immediately
     *
     * Used for frames requiring immediate transmission such as PATH_CHALLENGE,
     * PATH_RESPONSE, or CONNECTION_CLOSE.
     *
     * @param frame Frame to send
     * @param level Encryption level (defaults to current level)
     * @return true if successfully sent
     */
    bool SendImmediateFrame(std::shared_ptr<IFrame> frame, EncryptionLevel level = kApplication);

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

    // Flow controller accessors for use by FrameProcessor and StreamManager
    SendFlowController& GetSendFlowController() { return send_flow_controller_; }
    RecvFlowController& GetRecvFlowController() { return recv_flow_controller_; }

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

    // IConnectionEventSink - Event interface to replace callbacks
    virtual void OnStreamDataReady(std::shared_ptr<IStream> stream) override;
    virtual void OnFrameReady(std::shared_ptr<IFrame> frame) override;
    virtual void OnConnectionActive() override;
    virtual void OnStreamClosed(uint64_t stream_id) override;
    virtual void OnConnectionClose(uint64_t error, uint16_t frame_type, const std::string& reason) override;

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

    void OnTransportParams(TransportParam& remote_tp);

private:
    // Internal helper methods for TrySend()

    /**
     * @brief Send buffer using sender_ (internal helper)
     * @param buffer Buffer to send
     * @return true if successfully sent
     */
    bool SendBuffer(std::shared_ptr<common::IBuffer> buffer);

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

    // Immediate send for critical frames (ACK, PATH_CHALLENGE/RESPONSE, CONNECTION_CLOSE)
    // Bypasses normal send path and uses sender_ directly
    bool SendImmediate(std::shared_ptr<common::IBuffer> buffer);

    void InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string reason);
    void ImmediateClose(uint64_t error, uint16_t tigger_frame, std::string reason);
    void InnerStreamClose(uint64_t stream_id);

    // Stream data ACK notification callback
    void OnStreamDataAcked(uint64_t stream_id, uint64_t max_offset, bool has_fin);

    // Retry pending stream creation requests after receiving MAX_STREAMS
    void RetryPendingStreamRequests();

    void AddConnectionId(ConnectionID& id);
    void RetireConnectionId(ConnectionID& id);

    virtual void WriteCryptoData(std::shared_ptr<IBufferRead> buffer, int32_t err, uint16_t encryption_level) = 0;

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

    // RFC 9000 Section 9: Connection Migration
    // Simple API: delegates to production API with current IP and system-chosen port
    // This ensures interop tests use the same code path as production
    virtual bool InitiateMigration() override;

    // Full migration API (production use - specify new local address)
    virtual MigrationResult InitiateMigrationTo(const std::string& local_ip, uint16_t local_port = 0) override;

    // Set callback for migration events
    virtual void SetMigrationCallback(migration_callback cb) override;

    // Get current local address
    virtual void GetLocalAddr(std::string& addr, uint32_t& port) override;

    // Check if migration is supported (peer didn't disable it)
    virtual bool IsMigrationSupported() const override;

    // Check if migration is in progress
    virtual bool IsMigrationInProgress() const override;

    // Internal: Handle migration completion callback from PathManager
    void OnMigrationComplete(const MigrationInfo& info);

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
    SendFlowController send_flow_controller_;  // Send-side flow controller
    RecvFlowController recv_flow_controller_;  // Receive-side flow controller
    RecvControl recv_control_;
    SendManager send_manager_;
    // crypto
    ConnectionCrypto connection_crypto_;
    // Encryption level scheduler (centralized encryption level selection)
    std::unique_ptr<EncryptionLevelScheduler> encryption_scheduler_;
    // Packet builder for unified packet construction
    std::unique_ptr<PacketBuilder> packet_builder_;
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

    // Remembered remote transport params for 0-RTT session caching (RFC 9000 Section 7.4.1)
    bool has_remote_tp_ = false;
    uint32_t remote_initial_max_data_ = 0;
    uint32_t remote_initial_max_streams_bidi_ = 0;
    uint32_t remote_initial_max_streams_uni_ = 0;
    uint32_t remote_initial_max_stream_data_bidi_local_ = 0;
    uint32_t remote_initial_max_stream_data_bidi_remote_ = 0;
    uint32_t remote_initial_max_stream_data_uni_ = 0;
    uint32_t remote_active_connection_id_limit_ = 0;

    // EventLoop reference for safe cleanup in destructor
    std::shared_ptr<common::IEventLoop> event_loop_;

    // Metrics: Handshake timing
    uint64_t handshake_start_time_{0};

    // Sender for direct packet transmission (改动5: Sender down-shift)
    std::shared_ptr<ISender> sender_;

    // Key Update trigger (RFC 9001 Section 6)
    KeyUpdateTrigger key_update_trigger_;

    // QUIC version for this connection (RFC 9369: default to v2)
    uint32_t quic_version_ = 0x6b3343cf;

    // Version negotiation (RFC 9000 Section 6)
    version_negotiation_callback version_negotiation_cb_;
    uint32_t negotiated_version_ = 0;
    bool version_negotiation_needed_ = false;
    bool version_negotiation_done_ = false;  // Prevent infinite version negotiation loops
};

}  // namespace quic
}  // namespace quicx

#endif