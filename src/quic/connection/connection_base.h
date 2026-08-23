#ifndef QUIC_CONNECTION_CONNECTION_BASE
#define QUIC_CONNECTION_CONNECTION_BASE

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <quicx/common/if_event_loop.h>

#include <quicx/quic/type.h>
#include "quic/connection/connection_crypto.h"
#include "quic/connection/connection_id_coordinator.h"
#include "quic/connection/connection_id_manager.h"
#include "quic/connection/connection_path_manager.h"
#include "quic/connection/connection_state_machine.h"
#include "quic/connection/controler/recv_control.h"
#include "quic/connection/controler/recv_flow_controller.h"
#include "quic/connection/controler/send_flow_controller.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/datagram_emitter.h"
#include "quic/connection/if_connection.h"
#include "quic/connection/if_connection_event_sink.h"
#include "quic/connection/key_update_trigger.h"
#include "quic/connection/migration_controller.h"
#include "quic/connection/remote_transport_param_snapshot.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/version_negotiator.h"
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
    BaseConnection(StreamIDGenerator::StreamStarter start, bool ecn_enabled, std::shared_ptr<common::IEventLoop> loop,
        const ConnectionCallbacks& callbacks);
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
    virtual uint64_t AddTimer(timer_callback callback, uint32_t timeout_ms,
                               bool periodic = false) override;
    virtual void RemoveTimer(uint64_t timer_id) override;
    virtual bool IsTerminating() const override;

    // RFC 9001 Section 6: Key Update support
    void SetKeyUpdateEnabled(bool enabled) { key_update_trigger_.SetEnabled(enabled); }
    void SetKeyUpdateForced(bool forced) { key_update_trigger_.SetForce(forced); }
    bool TriggerKeyUpdate() { return connection_crypto_.TriggerKeyUpdate(); }

    // RFC 9369 / RFC 9368 / RFC 9000 §6: version state lives in
    // VersionNegotiator, which keeps the three copies of the on-wire version
    // (this layer, ConnectionCrypto, and the version_information TP) in step.
    // These are thin forwarders; there is no version state here.
    void SetVersion(uint32_t version) { version_negotiator_->ApplyVersion(version); }
    uint32_t GetVersion() const { return version_negotiator_->GetVersion(); }

    // RFC 9368 Compatible Version Negotiation: the application-preferred version.
    // This is the version we *want* to end up using for 1-RTT; if different
    // from the current wire version we advertise willingness to upgrade via
    // version_information. Defaults to the wire version (no upgrade desired).
    void SetPreferredVersion(uint32_t version) { version_negotiator_->SetPreferredVersion(version); }
    uint32_t GetPreferredVersion() const { return version_negotiator_->GetPreferredVersion(); }

    typedef VersionNegotiator::version_negotiation_callback version_negotiation_callback;
    void SetVersionNegotiationCallback(version_negotiation_callback cb) {
        version_negotiator_->SetVersionNegotiationCallback(std::move(cb));
    }
    void SetVersionNegotiationDone() { version_negotiator_->SetVersionNegotiationDone(); }

    // *************** inner interface ***************//
    // set transport param
    void AddTransportParam(const QuicTransportParams& tp_config) override;
    virtual uint64_t GetConnectionIDHash() override;

    // ==================== New High-Level Send Interfaces ====================

    // Main send interface (replaces GenerateSendData)
    // Called by Worker to attempt sending data. Internally decides whether to send,
    // what to send, and handles all packet building and transmission.
    // @return true if successfully sent data, false if no data or send failed
    // Optimised multi-packet send entry point. Reuses encryption-level
    // scheduling context, cryptographer lookup and packet-builder fixed-field
    // setup across inner iterations; only the per-packet state (cwnd headroom,
    // pending frames, FC slack, chunk allocation, build, send, post-send
    // bookkeeping) is recomputed. Returns the number of packets actually
    // emitted in this call (<= budget).
    int TrySendBurst(int budget) override;

    // Send ACK packet immediately
    // Simplified interface for immediate ACK sending, used for cross-level ACKs
    // or when immediate ACK is required.
    // @param ns Packet number space
    // @return true if successfully sent
    bool SendImmediateAck(PacketNumberSpace ns);

    // Send a single-frame probe packet (e.g. PATH_CHALLENGE for path
    // validation) immediately in 1-RTT, bypassing the send queue and the
    // end-of-round batch flush. Used when the packet must be the first one
    // a new path sees, so it cannot wait behind queued frames.
    // @param frame probe frame to send (PATH_CHALLENGE / PATH_RESPONSE)
    // @return true if the packet was built and sent
    bool SendImmediateProbe(const std::shared_ptr<IFrame>& frame);

    // handle packets
    virtual void OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets, uint32_t datagram_size) override;
    virtual void SetPendingEcn(uint8_t ecn) override { pending_ecn_ = ecn; }
    virtual EncryptionLevel GetCurEncryptionLevel() override;

    // observed peer address from network; store as candidate if different
    virtual void OnObservedPeerAddress(const common::Address& addr) override;

    // Get all local CID hashes for this connection (for cleanup on close)
    virtual std::vector<uint64_t> GetAllLocalCIDHashes() override { return cid_coordinator_->GetAllLocalCIDHashes(); }


    std::shared_ptr<common::IEventLoop> GetEventLoop() { return event_loop_.lock(); }

    // Get qlog trace for this connection
    std::shared_ptr<common::QlogTrace> GetQlogTrace() const override { return qlog_trace_; }

    // Production accessor used by Worker to gate early-connection delivery
    // (see IConnection::HasEarlyDataWriteKey doc). Implemented as a thin
    // wrapper over connection_crypto_; cheap, non-allocating.
    bool HasEarlyDataWriteKey() const override { return connection_crypto_.GetCryptographer(kEarlyData) != nullptr; }

    // ==================== Test-Only Accessors ====================
    // These methods exist solely for unit testing. Production code MUST NOT call them.
    // They are deliberately non-virtual so they do not appear on the IConnection
    // vtable — tests get to them by holding a concrete BaseConnection-derived
    // type (ClientConnection / ServerConnection), which is already how the
    // existing tests are written.
    //
    // Long-term: collapse this group behind a `friend class TestAccessor`
    // so the production API surface is fully clean. Tracked in
    // learning_project_roadmap.md §2 — interface hardening is post-1.0
    // since it would churn many test files for no teaching benefit.
    std::shared_ptr<ICryptographer> GetCryptographerForTest(uint16_t level) {
        return connection_crypto_.GetCryptographer(level);
    }
    std::shared_ptr<ConnectionIDManager> GetRemoteConnectionIDManagerForTest() {
        return cid_coordinator_->GetRemoteConnectionIDManagerForTest();
    }
    ConnectionStateType GetConnectionStateForTest() const { return state_machine_.GetState(); }

    uint32_t GetQuicVersionForTest() const { return version_negotiator_->GetVersion(); }
    // The versionConnectionCrypto holds. This is the copy the send path
    // actually reads, so it drifting from GetQuicVersionForTest() would put the
    // wrong version on the wire while every connection-level assertion still
    // passed.
    uint32_t GetCryptoVersionForTest() const { return connection_crypto_.GetVersion(); }
    bool IsServerForTest() const { return is_server_; }
    bool CompatVnCompletedForTest() const { return version_negotiator_->IsCompatVnCompleted(); }
    const TransportParam& GetLocalTransportParamForTest() const { return transport_param_; }
    const std::string& GetInitialSecretDcidForTest() const { return connection_crypto_.GetInitialSecretDcid(); }
    // Lets tests assert the RFC 9000 §8.1 budget is engaged on the real send
    // path rather than on a stand-alone controller instance.
    SendManager& GetSendManagerForTest() { return send_manager_; }
    // ==================== End Test-Only Accessors ====================

    // IConnectionStateListener
    virtual void OnStateToConnecting() override {}
    virtual void OnStateToConnected() override;
    virtual void OnStateToClosing() override;
    virtual void OnStateToDraining() override;
    virtual void OnStateToClosed() override;

    // Called from OnPackets after a 1-RTT (application-data) packet is
    // successfully decrypted and dispatched. The client overrides this to
    // confirm the handshake per RFC 9000 §4.1.2 (a client MUST consider the
    // handshake confirmed upon receiving a 1-RTT packet), which is essential
    // when the HANDSHAKE_DONE frame is lost under packet corruption/loss.
    virtual void OnApplicationDataPacketProcessed() {}

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
    // |cryptographer_override|, when non-null, is used instead of the keys the
    // connection currently holds for this packet's crypto level. Needed by
    // OnInitialPacket: after an RFC 9368 compatible upgrade, which Initial keys
    // can read a packet depends on the version in its long header, not on the
    // connection's current version.
    bool OnNormalPacket(
        const std::shared_ptr<IPacket>& packet, const std::shared_ptr<ICryptographer>& cryptographer_override = nullptr);
    virtual bool OnHandshakePacket(const std::shared_ptr<IPacket>& packet);
    virtual bool OnRetryPacket(const std::shared_ptr<IPacket>& packet) = 0;

    // handle frames (delegated to frame processor)
    bool OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level);

    void OnTransportParams(TransportParam& remote_tp);

    // RFC 9368 §4 validation of the peer's version_information (and the
    // server-side compatible upgrade it may trigger). Forwards to
    // VersionNegotiator; returns false if the connection was closed as
    // inconsistent.
    bool ValidateAndMaybeUpgradeByRemoteTP(const TransportParam& remote_tp) {
        return version_negotiator_->ValidateAndMaybeUpgradeByRemoteTP(remote_tp);
    }

    // RFC 9000 §7.3: cross-checks the connection IDs the peer authenticated inside
    // its transport parameters against the ones we actually observed on the wire.
    // This is what stops an off-path attacker from steering either endpoint onto a
    // connection ID it never chose (and, on the client, from forging a Retry).
    // Returns false when the connection has been closed as a violation.
    // The base implementation covers initial_source_connection_id, which both roles
    // must verify; ClientConnection extends it with the server-only IDs.
    virtual bool ValidatePeerConnectionIds(const TransportParam& remote_tp);

    /**
     * @brief Detect an RFC 9000 §10.3 Stateless Reset in a datagram we failed to
     *        process, and tear the connection down if so.
     *
     * A Stateless Reset is how a peer that has lost our connection state (restart,
     * crash, LB reroute) tells us to stop. Without this the connection sits
     * retransmitting until the idle timeout.
     *
     * Only called once normal processing has failed, since a reset is by design
     * indistinguishable from a 1-RTT packet until the token is matched.
     *
     * @return true if a reset was recognised and the connection terminated.
     */
    bool CheckStatelessReset(const std::vector<std::shared_ptr<IPacket>>& packets);

    // Shortest datagram that could carry a Stateless Reset: 5 unpredictable bytes
    // plus the 16-byte token (RFC 9000 §10.3).
    static constexpr uint32_t kMinStatelessResetSize = 21;

    // Peer's Source Connection ID as seen in the first long-header packet it sent.
    // Captured verbatim because the CID managers rotate over the connection's life,
    // while §7.3 compares against this first observation.
    const std::string& GetPeerInitialScid() const { return peer_initial_scid_; }
    void RecordPeerInitialScid(const uint8_t* id, uint8_t len);

private:
    // Encode the local TransportParam |tp| into a 1024-byte stack buffer and
    // hand the bytes to TLS via SSL_set_quic_transport_params.  Centralizes the
    // serialize-and-push idiom. Also serves VersionNegotiator, which needs to
    // re-push after a version change but has no business knowing about TLS.
    // Returns false on encode error.
    bool EncodeAndPushTpToTls(TransportParam& tp);

    // Internal helper methods for the send path

    // Retransmit-side branch: re-encode the next lost packet (with a fresh
    // packet number, current key phase, current cryptographer) and hand it to
    // the emitter. Caller MUST have already verified
    // SendControl::NeedReSend() returns true.
    // Returns true if the worker should re-enter the send path immediately
    // (more lost packets queued, or transient drop), false otherwise.
    // RFC 9000 §13.3 / RFC 9001 §6.5.
    bool TrySendRetransmit();

    // Normal-send branch: pick encryption level via the encryption scheduler,
    // gather pending frames + queued ACKs + stream data, build outbound packets
    // under cwnd / per-packet MTU / connection-level FC budgets, hand them to
    // the emitter, and update post-send bookkeeping (FC accounting, key-update
    // trigger).
    //
    // Caller passes the maximum number of packets allowed in this round; the
    // routine reuses send context / cryptographer / packet-builder fixed fields
    // across iterations and breaks out early if any inner step says we should
    // yield (cwnd full, FC blocked, no data, build failure, encryption level
    // change, etc.). Returns the actual packets emitted (>=0).
    int TrySendNewBurst(int budget);

    // Fill the identity half of a data-packet build context: the level we are
    // encrypting at, the cryptographer and CID managers to use, the version, and
    // the stream source. These are derived identically for every outbound data
    // packet, so they live here rather than being re-listed at each build site —
    // there are three such sites (the burst loop plus the Initial and Handshake
    // halves of a coalesced datagram), which meant adding a field was three
    // edits and forgetting one was silent.
    //
    // Deliberately does NOT touch padding, size limits, token, key phase or
    // frames: those are exactly what legitimately differs between the
    // single-level burst and the coalesced datagram, and defaulting them here
    // would hide that.
    void FillPacketIdentity(
        PacketBuilder::DataPacketContext& ctx, EncryptionLevel level, std::shared_ptr<ICryptographer> cryptographer);

    // Prepend an ACK for |ns| to |frames| if RecvControl says one is due.
    //
    // Used by the coalescing path, which asks RecvControl once per encryption
    // level. The burst path deliberately does NOT use this: it takes the
    // pending-ACK decision from the encryption-level scheduler
    // (send_ctx.has_pending_ack / ack_space) and attaches at most one ACK per
    // burst. Those are different policies, not a duplication to be merged.
    void MaybePrependAck(std::vector<std::shared_ptr<IFrame>>& frames, PacketNumberSpace ns);

    // RFC 9000 §12.2 Initial+Handshake coalescing helpers.
    //
    // TryCoalescedInitialHandshake: detects whether both Initial and
    // Handshake have pending CRYPTO bytes in the current round; if yes,
    // builds one UDP datagram containing both QUIC packets and ships it
    // through the emitter. Returns the number of QUIC packets emitted
    // (0 = not applicable / build failure, 2 = coalesced).
    // A return of 0 is non-fatal: the caller falls back to the normal
    // level-sticky TrySendNewBurst path.
    //
    // Padding (RFC 9000 §14.1) is carried *inside the Handshake packet*,
    // not the Initial packet. This is legal because §14.1 mandates the
    // datagram (not Initial specifically) be >= 1200 B, and §12.2
    // explicitly lists coalescing as one of the two means of meeting
    // that requirement. Carrying padding in Handshake keeps the Initial
    // small (cheaper for the peer's initial-keys AEAD path) and lets us
    // build packets in their natural on-wire order without needing a
    // back-patch / temporary buffer.
    int TryCoalescedInitialHandshake();

    // Enqueue a frame that must go out even though the connection is
    // terminating, bypassing the wake-up suppression in OnConnectionActive().
    // Only CONNECTION_CLOSE legitimately needs this.
    void EnqueueFrameDuringTermination(std::shared_ptr<IFrame> frame);

public:
    // PERF (sendmmsg batch path): when set non-null, the emitter appends built
    // NetPackets to *sink instead of calling sender_->Send() directly. The owner
    // (Worker::ProcessSend) then issues a single sender_->SendBatch() over all
    // collected packets, replacing N sendto() syscalls with one sendmmsg(2).
    // Set to nullptr (the default) to keep the synchronous-send-per-buffer
    // behaviour — used by paths that don't go through ProcessSend (e.g.
    // handshake bring-up before the connection is in the active set).
    //
    // The pointer is owned by the caller and must outlive every send between
    // Set/clear. Worker installs and clears it inside a single ProcessSend
    // iteration so lifetime is trivially correct.
    void SetSendSink(std::vector<std::shared_ptr<NetPacket>>* sink) override { emitter_->SetBatchSink(sink); }

    /**
     * @brief Put the connection under the RFC 9000 §8.1 anti-amplification budget.
     *
     * A server MUST call this for every connection created from an Initial packet
     * whose source address is not yet validated (i.e. one that did not carry a
     * valid Retry token). Until validation the connection may send at most three
     * times the bytes it has received, which is what stops a spoofed-source Initial
     * from eliciting a multi-kilobyte certificate chain at an attacker's victim.
     *
     * The budget is lifted automatically once a Handshake packet from the peer is
     * processed (see DispatchByType).
     */
    void EnterUnvalidatedAddressState() { send_manager_.ResetAmpBudget(/*initial_credit=*/0); }

    /**
     * @brief Lift the §8.1 budget because the peer address is proven.
     *
     * Normally driven automatically by processing a Handshake packet; the
     * accepting worker also calls it directly when the client echoed a valid
     * Retry token, which validates the address before the handshake completes
     * (RFC 9000 §8.1.2).
     */
    void MarkAddressValidated() { send_manager_.MarkAddressValidated(); }

protected:
    virtual void ThreadTransferBefore() override;
    virtual void ThreadTransferAfter() override;
    // idle timeout
    void OnIdleTimeout();
    void OnClosingTimeout();
    void CheckPTOTimeout();  // RFC 9002: Check for idle timeout from excessive PTOs

    // NB: frame-enqueue and send-wakeup live solely on the IConnectionEventSink
    // surface (OnFrameReady / OnStreamDataReady / OnConnectionActive). The old
    // ToSendFrame / ActiveSendStream / ActiveSend duplicates are gone: keeping
    // both meant one semantic had six entry names.

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

    // Internal: forward the migration-finished event to the application. Socket
    // switching and retiring is done by MigrationController before this runs.
    void OnMigrationFinished(const MigrationInfo& info);

public:
    // The active socket fd is owned by the emitter. Public because Worker
    // installs the socket after construction.
    virtual void SetSocket(int32_t sockfd) override { emitter_->SetSocket(sockfd); }

    // Socket register/unregister are consumed by MigrationController (the sole
    // owner of socket lifecycle), so forward them on rather than only storing.
    virtual void SetRegisterSocketCallback(RegisterSocketCallback cb) override {
        IConnection::SetRegisterSocketCallback(cb);
        migration_controller_->SetRegisterSocketCallback(cb);
    }
    virtual void SetUnregisterSocketCallback(UnregisterSocketCallback cb) override {
        IConnection::SetUnregisterSocketCallback(cb);
        migration_controller_->SetUnregisterSocketCallback(cb);
    }

protected:
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

    // qlog draft-03: per-connection monotonic datagram id counters. Each
    // ingress/egress UDP datagram is stamped so that coalesced QUIC packets
    // can be grouped back to their enclosing datagram in viewers. 0 is the
    // "unset" sentinel; the first real id is 1.
    uint64_t next_recv_datagram_id_ = 1;
    // The id of the UDP datagram currently being drained inside OnPackets()
    // (set before the dispatch loop, cleared after). The per-packet
    // QLOG_PACKET_RECEIVED call sites read it to stamp PacketReceivedData
    // so each packet links back to its enclosing datagrams_received event.
    // 0 means "no datagram in flight".
    uint64_t current_recv_datagram_id_ = 0;
    // NB: outgoing datagram correlation lives inside SendControl
    // (see Begin/EndSendDatagram + current_send_datagram_id_ there) and the
    // egress id counter lives in DatagramEmitter, so we don't duplicate
    // either here.

    // NB: 0-RTT send ordering (formerly has_app_send_pending_ /
    // initial_packet_sent_ here) is owned by EncryptionLevelScheduler; see
    // SetEarlyDataPending() and SetInitialPacketSent() there. Do not
    // re-introduce copies.

    // Remembered remote transport params for 0-RTT session caching (RFC 9000 Section 7.4.1)
    RemoteTransportParamSnapshot remote_tp_snapshot_;

    // EventLoop reference — observer only (owner is QuicClient/QuicServer)
    std::weak_ptr<common::IEventLoop> event_loop_;

    // Metrics: Handshake timing.
    //
    // NOTE: this is a WALL-CLOCK timestamp (UTCTimeMsec, std::chrono::system_clock)
    // expressed in milliseconds since the Unix epoch. It is used only to compute
    // a one-shot, externally observable handshake-duration metric and is therefore
    // wall-clock on purpose so the value lines up with operator dashboards and qlog
    // timestamps.
    //
    // It is deliberately separate from the monotonic clocks used by the loss /
    // RTT / PTO machinery (see RttSampler, LossDetector). Do NOT use this field
    // for protocol timing decisions: a wall-clock jump backwards would silently
    // produce negative durations.
    uint64_t handshake_start_wall_time_ms_{0};

    // Sole owner of the egress path: sender, batch sink, datagram-id counter
    // and the active socket fd. See datagram_emitter.h for the invariants it
    // exists to hold.
    std::unique_ptr<DatagramEmitter> emitter_;

    // Sole owner of socket lifecycle across migration (RFC 9000 §9).
    std::unique_ptr<MigrationController> migration_controller_;

    // Preferred address advertised by the server while the connection was
    // still handshaking; the migration is launched at handshake completion
    // (see ClientConnection::HandleHandshakeDoneFrame). Valid only while the
    // string is non-empty.
    common::Address pending_preferred_addr_;
    bool has_pending_preferred_addr_ = false;

    // Key Update trigger (RFC 9001 Section 6)
    KeyUpdateTrigger key_update_trigger_;

    // Sole owner of QUIC version state (RFC 9000 §6, RFC 9368, RFC 9369). Its
    // VersionContext is private, so the three copies of the on-wire version
    // cannot drift via a stray field write from here.
    std::unique_ptr<VersionNegotiator> version_negotiator_;

    // Connection role. This is a construction-time constant, not negotiation
    // state, so it stays here rather than inside VersionNegotiator — the send
    // path needs it (client first flight must pad to 1200 B, RFC 9000 §14.1).
    const bool is_server_;

    // See GetPeerInitialScid(). Recorded once, on the peer's first long-header packet.
    std::string peer_initial_scid_;
    bool peer_initial_scid_recorded_{false};
};

}  // namespace quic
}  // namespace quicx

#endif