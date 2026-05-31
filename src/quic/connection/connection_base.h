#ifndef QUIC_CONNECTION_CONNECTION_BASE
#define QUIC_CONNECTION_CONNECTION_BASE

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <quicx/common/if_event_loop.h>

#include "quic/connection/connection_crypto.h"
#include "quic/connection/connection_id_coordinator.h"
#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_path_manager.h"
#include "quic/connection/connection_state_machine.h"
#include "quic/connection/controler/recv_control.h"
#include "quic/connection/controler/recv_flow_controller.h"
#include "quic/connection/controler/send_flow_controller.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/if_connection.h"
#include "quic/connection/if_connection_event_sink.h"
#include "quic/connection/key_update_trigger.h"
#include "quic/connection/remote_transport_param_snapshot.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/version_context.h"
#include <quicx/quic/type.h>
#include "quic/udp/if_sender.h"

namespace quicx {
namespace quic {

// Forward declarations for unique_ptr members not dereferenced in this header
class ConnectionCloser;
class FrameProcessor;
class StreamManager;
class TimerCoordinator;
class EncryptionLevelScheduler;
class PacketBuilder;

class BaseConnection:
    public IConnection,
    public IConnectionStateListener,
    public IConnectionEventSink,
    public std::enable_shared_from_this<BaseConnection> {
public:
    BaseConnection(StreamIDGenerator::StreamStarter start, bool ecn_enabled,
        std::shared_ptr<common::IEventLoop> loop, const ConnectionCallbacks& callbacks);
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
        version_ctx_.quic_version = version;
        connection_crypto_.SetVersion(version);
    }
    uint32_t GetVersion() const { return version_ctx_.quic_version; }

    // RFC 9368 Compatible Version Negotiation: the application-preferred version.
    // This is the version we *want* to end up using for 1-RTT; if different
    // from |quic_version| (which is the wire version of our initial Initial
    // packet) we will advertise willingness to upgrade via version_information.
    // Defaults to |quic_version| (no upgrade desired).
    void SetPreferredVersion(uint32_t version) { version_ctx_.preferred_version = version; }
    uint32_t GetPreferredVersion() const { return version_ctx_.GetEffectivePreferredVersion(); }

    // RFC 9000 Section 6: Version Negotiation
    typedef std::function<void(uint32_t new_version)> version_negotiation_callback;
    void SetVersionNegotiationCallback(version_negotiation_callback cb) { version_negotiation_cb_ = cb; }
    void SetVersionNegotiationDone() { version_ctx_.version_negotiation_done = true; }
    bool IsVersionNegotiationNeeded() const { return version_ctx_.version_negotiation_needed; }
    uint32_t GetNegotiatedVersion() const { return version_ctx_.negotiated_version; }

    // *************** inner interface ***************//
    // set transport param
    void AddTransportParam(const QuicTransportParams& tp_config) override;
    virtual uint64_t GetConnectionIDHash() override;

    // ==================== New High-Level Send Interfaces ====================

    // Main send interface (replaces GenerateSendData)
// Called by Worker to attempt sending data. Internally decides whether to send,
// what to send, and handles all packet building and transmission.
// @return true if successfully sent data, false if no data or send failed
    bool TrySend() override;

    // Send ACK packet immediately
// Simplified interface for immediate ACK sending, used for cross-level ACKs
// or when immediate ACK is required.
// @param ns Packet number space
// @return true if successfully sent
    bool SendImmediateAck(PacketNumberSpace ns);

    // Send single frame immediately
// Used for frames requiring immediate transmission such as PATH_CHALLENGE,
// PATH_RESPONSE, or CONNECTION_CLOSE.
// @param frame Frame to send
// @param level Encryption level (defaults to current level)
// @return true if successfully sent
    bool SendImmediateFrame(std::shared_ptr<IFrame> frame, EncryptionLevel level = kApplication);

    // handle packets
    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) override;
    virtual void SetPendingEcn(uint8_t ecn) override { pending_ecn_ = ecn; }
    virtual EncryptionLevel GetCurEncryptionLevel() override;

    // observed peer address from network; store as candidate if different
    virtual void OnObservedPeerAddress(const common::Address& addr) override;

    // Get all local CID hashes for this connection (for cleanup on close)
    virtual std::vector<uint64_t> GetAllLocalCIDHashes() override { return cid_coordinator_->GetAllLocalCIDHashes(); }

    // Flow controller accessors for use by FrameProcessor and StreamManager
    SendFlowController& GetSendFlowController() { return send_flow_controller_; }
    RecvFlowController& GetRecvFlowController() { return recv_flow_controller_; }

    std::shared_ptr<common::IEventLoop> GetEventLoop() { return event_loop_.lock(); }

    // Get qlog trace for this connection
    std::shared_ptr<common::QlogTrace> GetQlogTrace() const override { return qlog_trace_; }

    // ==================== Test-Only Accessors ====================
    // These methods exist solely for unit testing. Production code MUST NOT call them.
    // They are grouped here to make the test-production boundary explicit.
    // TODO: Move to a friend-class test accessor when IConnection virtual interface is refactored.
    virtual std::shared_ptr<ICryptographer> GetCryptographerForTest(uint16_t level) override {
        return connection_crypto_.GetCryptographer(level);
    }
    std::shared_ptr<ConnectionIDManager> GetRemoteConnectionIDManagerForTest() {
        return cid_coordinator_->GetRemoteConnectionIDManagerForTest();
    }
    ConnectionStateType GetConnectionStateForTest() const { return state_machine_.GetState(); }
    uint32_t GetQuicVersionForTest() const { return version_ctx_.quic_version; }
    bool IsServerForTest() const { return version_ctx_.is_server; }
    bool CompatVnCompletedForTest() const { return version_ctx_.compat_vn_completed; }
    const TransportParam& GetLocalTransportParamForTest() const { return transport_param_; }
    const std::string& GetInitialSecretDcidForTest() const {
        return connection_crypto_.GetInitialSecretDcid();
    }
    // ==================== End Test-Only Accessors ====================

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
    // OnPackets helpers (split from monolithic OnPackets for readability)
    void HandlePacketsInClosingState(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets);
    void DropPacketsInDrainingState(std::vector<std::shared_ptr<IPacket>>& packets);
    bool DispatchByType(const std::shared_ptr<IPacket>& packet);

    bool OnInitialPacket(const std::shared_ptr<IPacket>& packet);
    bool On0rttPacket(const std::shared_ptr<IPacket>& packet);
    bool On1rttPacket(const std::shared_ptr<IPacket>& packet);
    bool OnNormalPacket(const std::shared_ptr<IPacket>& packet);
    bool OnVersionNegotiationPacket(const std::shared_ptr<IPacket>& packet);
    virtual bool OnHandshakePacket(const std::shared_ptr<IPacket>& packet);
    virtual bool OnRetryPacket(const std::shared_ptr<IPacket>& packet) = 0;

    // OnVersionNegotiationPacket helpers
    bool IsVnDowngradeAttack(const std::vector<uint32_t>& supported_versions);
    void HandleCompatibleVersionFound(uint32_t compatible_version);

    // handle frames (delegated to frame processor)
    bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level);

    void OnTransportParams(TransportParam& remote_tp);

    // RFC 9368 Compatible Version Negotiation helpers.
    //
    // BuildLocalVersionInformation:
    //   Populate |tp| with the local endpoint's version_information TP value.
    //   chosen_version   = quic_version_ (the version we are currently using in
    //                      our Initial packets).
    //   available_versions = kQuicVersions (our preference list).
    //
    // ValidateAndMaybeUpgradeByRemoteTP:
    //   Called during handshake completion after receiving the remote peer's
    //   transport parameters. Performs:
    //     1. Consistency check on remote's chosen_version.
    //     2. Client-side: downgrade-attack detection against server's
    //        available_versions list.
    //     3. Server-side: decide whether to upgrade to a compatible preferred
    //        version based on the client's available_versions list. If so,
    //        updates quic_version_ and re-derives Initial keys.
    //   Returns false if a downgrade attack or protocol violation is detected;
    //   the caller should then close the connection with VERSION_NEGOTIATION_ERROR.
    void BuildLocalVersionInformation(TransportParam& tp) const;
    bool ValidateAndMaybeUpgradeByRemoteTP(const TransportParam& remote_tp);

private:
    // Internal helper methods for TrySend()

    // Send buffer using sender_ (internal helper)
// @param buffer Buffer to send
// @return true if successfully sent
    bool SendBuffer(std::shared_ptr<common::IBuffer> buffer);

public:
    // PERF (sendmmsg batch path): when set non-null, SendBuffer() appends the
    // built NetPacket to *send_sink_ instead of immediately calling
    // sender_->Send(). The owner (Worker::ProcessSend) then issues a single
    // sender_->SendBatch() over all collected packets, replacing N sendto()
    // syscalls with one sendmmsg(2). Set to nullptr (the default) to keep
    // the legacy synchronous-Send-per-buffer behavior — used by paths that
    // don't go through ProcessSend (e.g. handshake bring-up before the
    // connection is in the active set).
    //
    // The pointer is owned by the caller and must outlive every SendBuffer
    // call between Set/clear. Worker installs and clears it inside a single
    // ProcessSend iteration so lifetime is trivially correct.
    void SetSendSink(std::vector<std::shared_ptr<NetPacket>>* sink) override {
        send_sink_ = sink;
    }

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

    void InnerConnectionClose(uint64_t error, uint16_t trigger_frame, std::string reason);
    void ImmediateClose(uint64_t error, uint16_t trigger_frame, std::string reason);
    void InnerStreamClose(uint64_t stream_id);

    // Stream data ACK notification callback
    void OnStreamDataAcked(uint64_t stream_id, uint64_t offset_start, uint64_t length, bool has_fin);

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
    RemoteTransportParamSnapshot remote_tp_snapshot_;

    // EventLoop reference — observer only (owner is QuicClient/QuicServer)
    std::weak_ptr<common::IEventLoop> event_loop_;

    // Metrics: Handshake timing
    uint64_t handshake_start_time_{0};

    // Sender for direct packet transmission
    std::shared_ptr<ISender> sender_;

    // Optional batch sink for SendBuffer (see SetSendSink). Non-owning.
    // Worker::ProcessSend installs this for the duration of a single drain
    // round and clears it before returning, so liveness is always correct.
    std::vector<std::shared_ptr<NetPacket>>* send_sink_ = nullptr;

    // Key Update trigger (RFC 9001 Section 6)
    KeyUpdateTrigger key_update_trigger_;

    // QUIC version context (RFC 9000 §6, RFC 9368, RFC 9369)
    VersionContext version_ctx_;

    // Version negotiation callback (RFC 9000 Section 6)
    version_negotiation_callback version_negotiation_cb_;
};

}  // namespace quic
}  // namespace quicx

#endif