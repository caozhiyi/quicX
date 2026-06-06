#include <cstring>
#include <cstdio>

#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/qlog/qlog.h"
#include "common/util/time.h"
#include "common/buffer/buffer_span.h"
#include "common/network/io_handle.h"
#include "quic/common/constants.h"

#include "quic/common/version.h"
#include "quic/connection/connection_base.h"
#include "quic/connection/connection_closer.h"
#include "quic/connection/connection_frame_processor.h"
#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/connection_timer_coordinator.h"
#include "quic/connection/encryption_level_scheduler.h"
#include "quic/connection/error.h"
#include "quic/connection/util.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/ping_frame.h"
#include "quic/frame/type.h"
#include "quic/packet/packet_number.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/packet/type.h"
#include "quic/packet/version_negotiation_packet.h"
#include "quic/quicx/global_resource.h"
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

BaseConnection::BaseConnection(StreamIDGenerator::StreamStarter start, bool ecn_enabled,
    std::shared_ptr<common::IEventLoop> loop, const ConnectionCallbacks& callbacks):
    IConnection(callbacks),
    ecn_enabled_(ecn_enabled),
    recv_control_(loop->GetTimer()),
    send_manager_(loop->GetTimer()),
    event_loop_(loop),
    last_communicate_time_(0),
    send_flow_controller_(start),
    recv_flow_controller_(start),
    state_machine_(this),
    packet_builder_(std::make_unique<PacketBuilder>()) {
    version_ctx_.is_server = (start == StreamIDGenerator::StreamStarter::kServer);
    version_ctx_.quic_version = kQuicVersion2;
    // Metrics: Record handshake start time (wall clock; see field comment in
    // connection_base.h — this is intentionally NOT the monotonic clock used
    // for RTT/PTO).
    handshake_start_wall_time_ms_ = common::UTCTimeMsec();
    connection_crypto_.SetRemoteTransportParamCB(
        [this](auto& tp) { OnTransportParams(tp); });

    // RFC 9000: When 0-RTT write key is installed, trigger early connection callback
    // so the application can start sending data before the handshake completes
    connection_crypto_.SetEarlyDataReadyCB([this]() {
        LOG_INFO("0-RTT early data ready, triggering early connection callback");
        if (handshake_done_cb_) {
            handshake_done_cb_(shared_from_this());
        }
    });

    // RFC 9001 §4.8: surface a fatal TLS handshake alert as a CONNECTION_CLOSE so a
    // failed handshake does not silently hang the connection.
    connection_crypto_.SetHandshakeErrorCB([this](uint64_t error, const std::string& reason) {
        InnerConnectionClose(error, 0, reason);
    });

    // Initialize connection ID coordinator (refactored)
    cid_coordinator_ = std::make_unique<ConnectionIDCoordinator>(loop, send_manager_,
        [this](auto& cid) { AddConnectionId(cid); },
        [this](auto& cid) { RetireConnectionId(cid); });
    cid_coordinator_->Initialize();

    send_manager_.SetSendRetryCallBack([this]() { ActiveSend(); });
    send_manager_.SetSendFlowController(&send_flow_controller_);

    // RFC 9002 §6.2.2.1: During handshake, if PTO fires and no ACK-eliciting
    // data to retransmit, send PING to elicit ACK from peer (anti-amplification)
    send_manager_.GetSendControl().SetProbeNeededCallback([this]() {
        LOG_INFO("Handshake probe: sending PING frame to elicit ACK");
        auto ping = std::make_shared<PingFrame>();
        ToSendFrame(ping);
    });

    // RFC 9002 §6.2.4 (Bug-19): post-handshake PTO probe.
    // When PTO fires after the handshake is complete, queue a PING in 1-RTT
    // (Application) space *in addition* to retransmitting the oldest unacked
    // packet. This guarantees the probe round trip is ack-eliciting even
    // when every retransmitted byte keeps losing on a high-loss path. The
    // PING frame is tiny (1 byte) and its packet (~22 bytes encrypted) is
    // not subject to stream flow control, so it works even when both
    // streams and the connection-level FC window are exhausted — which is
    // precisely the deadlock that left transfer-5MB stalled until idle
    // timeout.  Trigger ActiveSend here too so the worker re-enters the
    // send loop on the same thread (otherwise the queued PING would just
    // sit in wait_frame_list_ until the next external event).
    send_manager_.GetSendControl().SetApplicationProbeCallback([this]() {
        LOG_INFO("Post-handshake PTO probe: queueing PING frame to elicit ACK");
        auto ping = std::make_shared<PingFrame>();
        ToSendFrame(ping);
        ActiveSend();
    });

    // RFC 9000: Setup immediate ACK callback for Initial/Handshake/out-of-order packets
    recv_control_.SetImmediateAckCB([this](PacketNumberSpace ns) { SendImmediateAck(ns); });

    // Setup delayed ACK callback for normal Application packets
    recv_control_.SetActiveSendCB([this]() { ActiveSend(); });

    transport_param_.AddTransportParamListener(
        [this](const auto& tp) { recv_control_.UpdateConfig(tp); });
    transport_param_.AddTransportParamListener(
        [this](const auto& tp) { send_manager_.UpdateConfig(tp); });
    transport_param_.AddTransportParamListener(
        [this](const auto& tp) { send_flow_controller_.UpdateConfig(tp); });
    transport_param_.AddTransportParamListener(
        [this](const auto& tp) { recv_flow_controller_.UpdateConfig(tp); });

    // Set stream data ACK callback for tracking stream completion
    send_manager_.send_control_.SetStreamDataAckCallback(
        [this](auto a, auto b, auto c, auto d) { OnStreamDataAcked(a, b, c, d); });

    // Initialize timer coordinator (refactored)
    timer_coordinator_ =
        std::make_unique<TimerCoordinator>(loop, transport_param_, send_manager_, state_machine_);

    // Initialize path manager (refactored)
    // Constructor takes a single Deps struct (named-field init) instead of
    // an 8-positional-argument list — see PathManager::Deps in
    // connection_path_manager.h for the lifetime contract.
    PathManager::Deps path_deps;
    path_deps.event_loop       = loop;
    path_deps.send_manager     = &send_manager_;
    path_deps.cid_coordinator  = cid_coordinator_.get();
    path_deps.transport_param  = &transport_param_;
    path_deps.peer_addr        = &peer_addr_;
    path_deps.to_send_frame_cb = [this](auto&& f) { ToSendFrame(std::forward<decltype(f)>(f)); };
    path_deps.active_send_cb   = [this]() { ActiveSend(); };
    path_deps.set_peer_addr_cb = [this](const common::Address& addr) { this->SetPeerAddress(addr); };
    path_manager_ = std::make_unique<PathManager>(std::move(path_deps));

    // Initialize encryption level scheduler (refactored) - centralizes encryption level selection
    encryption_scheduler_ =
        std::make_unique<EncryptionLevelScheduler>(connection_crypto_, recv_control_, *path_manager_);

    // Initialize stream manager (refactored) - uses IConnectionEventSink interface (no callbacks!)
    stream_manager_ = std::make_unique<StreamManager>(
        *this, loop, transport_param_, send_manager_, stream_state_cb_, &send_flow_controller_);

    // Inject stream manager into send manager for stream scheduling
    send_manager_.SetStreamManager(stream_manager_.get());

    // Initialize connection closer (refactored)
    connection_closer_ = std::make_unique<ConnectionCloser>(
        loop, state_machine_, send_manager_, transport_param_, connection_close_cb_);

    // Initialize frame processor (refactored) - uses IConnectionEventSink interface (no callbacks!)
    frame_processor_ = std::make_unique<FrameProcessor>(*this, state_machine_, connection_crypto_, send_manager_,
        *stream_manager_, *cid_coordinator_, *path_manager_, *connection_closer_, transport_param_, token_,
        &send_flow_controller_, &recv_flow_controller_);
    // Set application-level callbacks only
    frame_processor_->SetStreamStateCallback(stream_state_cb_);

    // Metrics: Connection created
    common::Metrics::GaugeInc(common::MetricsStd::QuicConnectionsActive);
    common::Metrics::CounterInc(common::MetricsStd::QuicConnectionsTotal);
}

BaseConnection::~BaseConnection() {
    // Metrics: Connection closed
    common::Metrics::GaugeDec(common::MetricsStd::QuicConnectionsActive);
    common::Metrics::CounterInc(common::MetricsStd::QuicConnectionsClosed);

    // Metrics: Record PTO count per connection
    uint32_t pto_count = send_manager_.GetRttCalculator().GetConsecutivePTOCount();
    common::Metrics::HistogramObserve(common::MetricsStd::PtoCountPerConnection, pto_count);

    // Clear stream manager first to prevent callbacks from accessing destroyed objects
    // Streams may still hold callbacks that reference stream_manager_, so we need to
    // ensure stream_manager_ is cleared before other members are destroyed
    stream_manager_.reset();
}

void BaseConnection::SetSender(std::shared_ptr<ISender> sender) {
    sender_ = sender;
    LOG_DEBUG("BaseConnection: Sender injected");
}

void BaseConnection::Close() {
    auto loop = event_loop_.lock();
    if (!loop) return;
    if (!loop->IsInLoopThread()) {
        auto weak_self = weak_from_this();
        loop->RunInLoop([weak_self]() {
            auto self = weak_self.lock();
            if (!self) return;
            self->CloseInternal();
        });
        return;
    }
    CloseInternal();
}

void BaseConnection::SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> active_cb) {
    active_connection_cb_ = active_cb;
}

void BaseConnection::CloseInternal() {
    if (!state_machine_.CanSendData()) {
        LOG_ERROR("BaseConnection::CloseInternal called in invalid state: %d", state_machine_.GetState());
        return;
    }
    LOG_INFO("BaseConnection::CloseInternal called");
    send_manager_.ClearActiveStreams();
    // Clear retransmission data to prevent retransmitting packets after close
    send_manager_.ClearRetransmissionData();

    // Delegate to connection closer
    connection_closer_->StartGracefulClose([this]() { ActiveSend(); });
}

void BaseConnection::Reset(uint32_t error_code) {
    ImmediateClose(error_code, 0, "application reset.");
}

std::shared_ptr<IQuicStream> BaseConnection::MakeStream(StreamDirection type) {
    // Delegate to stream manager
    return stream_manager_->MakeStreamWithFlowControl(type);
}

bool BaseConnection::MakeStreamAsync(StreamDirection type, stream_creation_callback callback) {
    // Thread-safety: MakeStreamAsync is the public entry point for the
    // HTTP/3 (and any other application-level) layer to create new QUIC
    // streams from an *application* thread (e.g. http3::Client::DoRequest
    // is invoked synchronously from the user's request thread, see
    // load_tester::RunClient). The underlying StreamManager owns
    // streams_map_ and pending_stream_requests_ as plain (non-locked)
    // containers because, by design, every other path that touches them
    // (FrameProcessor::OnAckFrame -> OnStreamDataAcked, OnStreamFrame,
    // CloseStream, ResetAllStreams, BuildStreamFrames, ...) runs on the
    // connection's event-loop thread.
    //
    // Without this hop the application thread races the event-loop
    // thread on streams_map_ — a load_tester run reproduces a SIGSEGV in
    // unordered_map::find(...) during StreamManager::OnStreamDataAcked
    // because a concurrent insert from the user thread is rehashing the
    // bucket array out from under it.
    //
    // RunInLoop short-circuits to a synchronous call when we are already
    // on the loop thread (e.g. ClientConnection::DoRequest -> ... ->
    // here, invoked from the OnConnection callback that itself runs on
    // the loop), so there is no extra latency for legitimate event-loop
    // callers.
    auto loop = event_loop_.lock();
    if (!loop) {
        // Connection is being torn down — there is no loop to dispatch
        // onto. Fail the request synchronously so the caller's promise
        // is resolved instead of being silently dropped.
        if (callback) {
            callback(nullptr);
        }
        return false;
    }

    if (loop->IsInLoopThread()) {
        return stream_manager_->MakeStreamAsync(type, callback);
    }

    // Cross-thread path: capture a weak_ptr so a connection that is
    // closed/destroyed before the task runs is safely observed via
    // weak_ptr::lock() instead of dereferencing freed memory.
    std::weak_ptr<BaseConnection> weak_self = std::static_pointer_cast<BaseConnection>(shared_from_this());
    loop->PostTask([weak_self, type, callback]() {
        auto self = weak_self.lock();
        if (!self) {
            if (callback) {
                callback(nullptr);
            }
            return;
        }
        self->stream_manager_->MakeStreamAsync(type, callback);
    });
    // The actual queue/created decision happens asynchronously; we only
    // know we successfully accepted the request.
    return true;
}

void BaseConnection::SetStreamStateCallBack(stream_state_callback cb) {
    // Update base class member
    stream_state_cb_ = cb;
    // Also update FrameProcessor's callback so it can notify HTTP/3 layer of new streams
    if (frame_processor_) {
        frame_processor_->SetStreamStateCallback(cb);
    }
    LOG_DEBUG(
        "BaseConnection::SetStreamStateCallBack: callback updated in both IConnection and FrameProcessor");
}

uint64_t BaseConnection::AddTimer(timer_callback callback, uint32_t timeout_ms) {
    if (!timer_coordinator_) {
        LOG_ERROR("BaseConnection::AddTimer: timer_coordinator_ is null");
        return 0;
    }
    return timer_coordinator_->AddTimer(callback, timeout_ms);
}

void BaseConnection::RemoveTimer(uint64_t timer_id) {
    if (!timer_coordinator_) {
        LOG_ERROR("BaseConnection::RemoveTimer: timer_coordinator_ is null");
        return;
    }
    timer_coordinator_->RemoveTimer(timer_id);
}

bool BaseConnection::IsTerminating() const {
    return state_machine_.IsTerminating();
}

void BaseConnection::RetryPendingStreamRequests() {
    // Delegate to stream manager
    stream_manager_->RetryPendingStreamRequests();
}

void BaseConnection::AddTransportParam(const QuicTransportParams& tp_config) {
    // RFC 9000 §18.2: Both endpoints MUST include initial_source_connection_id
    // in their transport parameters, set to the Source Connection ID field of the
    // first Initial packet they send. Automatically set from local CID if not
    // already provided by the caller.
    QuicTransportParams tp = tp_config;
    if (tp.initial_source_connection_id_.empty() && cid_coordinator_) {
        // RFC 9000 §5.1: If the local CID pool has not been primed yet, generate the
        // endpoint's first Source Connection ID now. The previous implementation relied
        // on ConnectionIDManager::GetCurrentID() lazily generating a CID on first read;
        // that implicit fallback has been removed because it allowed phantom CIDs to be
        // fabricated on the *remote* manager during connection migration. Generating
        // explicitly here keeps the local pool primed for both client and server.
        if (cid_coordinator_->GetLocalConnectionIDManager()->GetAvailableIDCount() == 0) {
            cid_coordinator_->GetLocalConnectionIDManager()->Generator();
        }
        auto local_scid = cid_coordinator_->GetLocalConnectionIDManager()->GetCurrentID();
        if (local_scid.GetLength() > 0) {
            tp.initial_source_connection_id_ =
                std::string(reinterpret_cast<const char*>(local_scid.GetID()), local_scid.GetLength());
        }
    }
    transport_param_.Init(tp);

    // RFC 9368 §3: Include the version_information (id 0x11) transport parameter.
    // chosen_version is the version this endpoint is currently using for its Initial
    // packets; available_versions is our preference-ordered supported list.
    BuildLocalVersionInformation(transport_param_);

    // Encode local transport parameters and hand them to TLS.  See
    // EncodeAndPushTpToTls() for the 1024-byte sizing rationale.
    //
    // RFC 9001 §4.1.3: Before starting the handshake, QUIC provides TLS with
    // the transport parameters (see Section 8.2) that it wishes to carry.
    if (!EncodeAndPushTpToTls(transport_param_)) {
        return;
    }
}

bool BaseConnection::EncodeAndPushTpToTls(TransportParam& tp) {
    if (!tls_connection_) return false;
    // Sizing rationale (1024 bytes): the QUIC v1 standard transport
    // parameters (RFC 9000 §18.2) plus version_information (RFC 9368 §3) add
    // up to well under 256 bytes in the worst case (~17 parameters × {varint
    // id + varint len + payload}, with the largest payloads being two 20-byte
    // connection-id strings, a few <=8-byte varints and a small fixed-size
    // preferred_address). 1024 leaves a 4× headroom for any future parameters
    // and for transport_param_.Encode's own bounds checking; the encoder will
    // fail closed via the BufferSpan overflow path if we ever exceed it.
    uint8_t tp_buffer[1024];
    size_t bytes_written = 0;
    common::BufferSpan buffer_span(tp_buffer, sizeof(tp_buffer));
    if (!tp.Encode(buffer_span, bytes_written)) {
        LOG_ERROR("encode transport param failed");
        return false;
    }
    return tls_connection_->AddTransportParam(tp_buffer, static_cast<uint32_t>(bytes_written));
}

bool BaseConnection::RebuildAndPushVersionInformation() {
    if (!transport_param_.HasVersionInformation()) {
        return false;
    }
    BuildLocalVersionInformation(transport_param_);
    return EncodeAndPushTpToTls(transport_param_);
}

bool BaseConnection::ValidateAndMaybeUpgradeByRemoteTP(const TransportParam& remote_tp) {
    // RFC 9368 §4: If the peer did not send version_information, there is nothing
    // to validate. This is also the case for endpoints speaking a version that
    // predates RFC 9368 — interop with those must continue to work.
    if (!remote_tp.HasVersionInformation()) {
        return true;
    }

    const uint32_t peer_chosen = remote_tp.GetChosenVersion();
    const std::vector<uint32_t>& peer_available = remote_tp.GetAvailableVersions();

    // RFC 9368 §4: The peer's chosen_version MUST match the version used on the
    // wire for the Initial packets that carried these transport parameters.
    //   - For client-received (server) TP: peer_chosen must equal version_ctx_.quic_version
    //     (the version of server-sent Initial packets, which by now equals what
    //     we have been decrypting with).
    //   - For server-received (client) TP: peer_chosen must equal the version
    //     the client used for its FIRST Initial (version_ctx_.original_version), which was
    //     recorded when the server first processed that packet. If the server
    //     has not yet recorded version_ctx_.original_version, fall back to current.
    const uint32_t expected_peer_on_wire = version_ctx_.is_server
        ? (version_ctx_.original_version != 0 ? version_ctx_.original_version : version_ctx_.quic_version)
        : version_ctx_.quic_version;

    if (peer_chosen != expected_peer_on_wire) {
        LOG_ERROR(
            "RFC 9368: peer chosen_version 0x%08x does not match version used on wire 0x%08x",
            peer_chosen, expected_peer_on_wire);
        InnerConnectionClose(QuicErrorCode::kVersionNegotiationError, 0,
            "version_information chosen_version mismatch");
        return false;
    }

    if (!version_ctx_.is_server) {
        // Client-side downgrade detection (RFC 9368 §4):
        // If the application explicitly specified a |version_ctx_.preferred_version| that
        // differs from the version we actually negotiated (version_ctx_.quic_version), and
        // the server's |available_versions| advertises that preferred version,
        // then a MITM may have stripped or rewrote our Initial to force a
        // downgrade.  Close with VERSION_NEGOTIATION_ERROR in that case.
        //
        // Without an explicit preference we have no way to know the "expected"
        // outcome, so we don't invent one.
        if (version_ctx_.preferred_version != 0 && version_ctx_.preferred_version != version_ctx_.quic_version) {
            for (uint32_t sv : peer_available) {
                if (sv == version_ctx_.preferred_version) {
                    LOG_ERROR(
                        "RFC 9368: downgrade detected: server advertises preferred 0x%08x "
                        "but connection ended up on 0x%08x",
                        version_ctx_.preferred_version, version_ctx_.quic_version);
                    InnerConnectionClose(QuicErrorCode::kVersionNegotiationError, 0,
                        "Compatible Version downgrade detected");
                    return false;
                }
            }
        }
        // Client side: no further action. Initial key rekey (if any) has
        // already happened via OnInitialPacket when the server's v2 Initial
        // arrived.
        version_ctx_.compat_vn_completed = true;
        return true;
    }

    // ------- Server side -------
    // Decide whether to upgrade from the client's chosen_version to a version we
    // prefer more. We only consider upgrading to |version_ctx_.preferred_version| (if the
    // application explicitly set one and it differs from |version_ctx_.quic_version|), and
    // only when the client's available_versions list also contains it. This
    // keeps the default path (no preference) conservative: a server without an
    // explicit preference just stays on the client's chosen_version.
    if (version_ctx_.compat_vn_completed) {
        return true;  // Already upgraded (or decided not to).
    }

    uint32_t negotiated = peer_chosen;  // Default: stay on client's chosen version.
    const uint32_t our_pref = (version_ctx_.preferred_version != 0) ? version_ctx_.preferred_version : version_ctx_.quic_version;
    if (our_pref != peer_chosen) {
        for (uint32_t cv : peer_available) {
            if (cv == our_pref) {
                negotiated = our_pref;
                break;
            }
        }
    }

    if (negotiated == version_ctx_.quic_version) {
        // No version change; but still record version_ctx_.original_version for later
        // consistency bookkeeping.
        if (version_ctx_.original_version == 0) {
            version_ctx_.original_version = peer_chosen;
        }
        version_ctx_.compat_vn_completed = true;
        return true;
    }

    // Upgrade! Re-derive Initial keys using the new version's salt with the
    // client's original DCID (same DCID the client used when sending its first
    // Initial). RFC 9368 §4: the DCID does not change across a compatible VN.
    LOG_INFO("RFC 9368: upgrading connection from 0x%08x to 0x%08x", version_ctx_.quic_version, negotiated);

    // Record the version the client used before we upgrade.
    if (version_ctx_.original_version == 0) {
        version_ctx_.original_version = peer_chosen;
    }

    // Obtain the client's original DCID. On the server this is the
    // original_destination_connection_id we advertised in our own TP, which is
    // the DCID the client sent in its first Initial.
    const std::string& odcid = transport_param_.GetOriginalDestinationConnectionId();
    if (odcid.empty()) {
        LOG_ERROR("RFC 9368: cannot upgrade, original_destination_connection_id missing");
        // Fall back to client's chosen_version (no upgrade) rather than failing.
        version_ctx_.compat_vn_completed = true;
        return true;
    }

    if (!connection_crypto_.RekeyInitialForVersion(
            negotiated,
            reinterpret_cast<const uint8_t*>(odcid.data()),
            static_cast<uint32_t>(odcid.size()),
            true /* is_server */)) {
        LOG_ERROR("RFC 9368: RekeyInitialForVersion failed");
        InnerConnectionClose(QuicErrorCode::kInternalError, 0, "Compatible VN rekey failed");
        return false;
    }

    // Update in-memory version so subsequent outbound Initial packets use the
    // new version (PacketBuilder reads version_ctx_.quic_version via connection_crypto_).
    version_ctx_.quic_version = negotiated;
    // Also update our local version_information TP so the TP we send to the
    // client (in EncryptedExtensions) reports chosen_version = negotiated, and
    // hand the re-encoded TP bytes to TLS before it serializes
    // EncryptedExtensions — otherwise the client will see chosen_version ==
    // original wire version and correctly reject the connection via the
    // RFC 9368 §4 consistency check.
    RebuildAndPushVersionInformation();

    version_ctx_.compat_vn_completed = true;
    common::Metrics::CounterInc(common::MetricsStd::VersionNegotiationTotal);
    return true;
}

void BaseConnection::BuildLocalVersionInformation(TransportParam& tp) const {
    // RFC 9368 §3: Build the local version_information TP value.
    //   chosen_version     = current |version_ctx_.quic_version| (the version actually on
    //                        the wire in our Initial packets).
    //   available_versions = versions we are willing to speak for this
    //                        connection, in preference order (most preferred
    //                        first).
    //
    // To keep interop with peers that predate RFC 9368 predictable, and to
    // avoid unsolicited upgrades in "default" scenarios, we only widen the
    // list beyond |version_ctx_.quic_version| when the application explicitly expressed a
    // different preferred version via |SetPreferredVersion|.
    //   - No preference set          => [version_ctx_.quic_version]  (1 entry)
    //   - Preference == version_ctx_.quic_version => [version_ctx_.quic_version]  (1 entry)
    //   - Preference != version_ctx_.quic_version => [version_ctx_.preferred_version, version_ctx_.quic_version]
    std::vector<uint32_t> available;
    const uint32_t pref = (version_ctx_.preferred_version != 0) ? version_ctx_.preferred_version : version_ctx_.quic_version;
    if (pref != version_ctx_.quic_version) {
        // Sanity-check: we only advertise versions we actually support.
        bool pref_supported = false;
        for (size_t i = 0; i < kQuicVersionsCount; i++) {
            if (kQuicVersions[i] == pref) { pref_supported = true; break; }
        }
        if (pref_supported) {
            available.push_back(pref);
        }
    }
    available.push_back(version_ctx_.quic_version);
    tp.SetVersionInformation(version_ctx_.quic_version, available);
}

uint64_t BaseConnection::GetConnectionIDHash() {
    return cid_coordinator_->GetConnectionIDHash();
}

void BaseConnection::OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    // Closing state: Check if packet contains CONNECTION_CLOSE, otherwise retransmit
    if (state_machine_.IsClosing()) {
        HandlePacketsInClosingState(now, packets);
        return;
    }

    // Draining or Closed state: Discard all packets
    if (state_machine_.ShouldIgnorePackets()) {
        DropPacketsInDrainingState(packets);
        return;
    }

    // Normal processing for Connecting/Connected states
    // Accumulate ECN to ACK_ECN counters based on first packet number space
    if (!packets.empty() && ecn_enabled_) {
        auto ns = CryptoLevel2PacketNumberSpace(packets[0]->GetCryptoLevel());
        recv_control_.OnEcnCounters(pending_ecn_, ns);
    }
    for (size_t i = 0; i < packets.size(); i++) {
        bool packet_processed = DispatchByType(packets[i]);

        // After processing (decrypting and decoding frames), record packet for ACK tracking
        if (packet_processed) {
            recv_control_.OnPacketRecv(now, packets[i]);
        }
    }

    // reset idle timeout timer task
    timer_coordinator_->ResetIdleTimer();
}

void BaseConnection::HandlePacketsInClosingState(uint64_t now,
    std::vector<std::shared_ptr<IPacket>>& packets) {
    bool has_connection_close = false;
    for (auto& packet : packets) {
        std::shared_ptr<ICryptographer> cryptographer =
            connection_crypto_.GetCryptographer(packet->GetCryptoLevel());
        if (cryptographer) {
            packet->SetCryptographer(cryptographer);

            // PERF: same situation as OnNormalPacket below — DecodeWithCrypto
            // ignores its IBuffer parameter in every implementation, so the
            // pooled chunk previously allocated here was wasted. This site is
            // not on the steady-state hot path (only fires while the connection
            // is in CLOSING) but the alloc is still pure overhead.
            if (packet->DecodeWithCrypto(nullptr)) {
                for (auto& frame : packet->GetFrames()) {
                    if (frame->GetType() == FrameType::kConnectionClose ||
                        frame->GetType() == FrameType::kConnectionCloseApp) {
                        has_connection_close = true;
                        state_machine_.OnConnectionCloseFrameReceived();
                        send_manager_.ClearRetransmissionData();
                        break;
                    }
                }
            } else {
                if (qlog_trace_) {
                    common::PacketDroppedData drop_data;
                    drop_data.packet_type = packet->GetHeader()->GetPacketType();
                    drop_data.packet_size = packet->GetSrcBuffer().GetLength();
                    drop_data.trigger = "closing_state_decrypt_failure";
                    QLOG_PACKET_DROPPED(qlog_trace_, drop_data);
                }
            }
        } else {
            if (qlog_trace_) {
                common::PacketDroppedData drop_data;
                drop_data.packet_type = packet->GetHeader()->GetPacketType();
                drop_data.trigger = "key_unavailable";
                QLOG_PACKET_DROPPED(qlog_trace_, drop_data);
            }
        }
        if (has_connection_close) {
            break;
        }
    }

    if (!has_connection_close) {
        uint64_t current_time = (now > 0) ? now : common::UTCTimeMsec();
        if (connection_closer_->ShouldRetransmitConnectionClose(current_time)) {
            auto frame = std::make_shared<ConnectionCloseFrame>();
            frame->SetErrorCode(connection_closer_->GetClosingErrorCode());
            frame->SetErrFrameType(connection_closer_->GetClosingTriggerFrame());
            frame->SetReason(connection_closer_->GetClosingReason());
            send_manager_.ToSendFrame(frame);
            if (active_connection_cb_) {
                active_connection_cb_(shared_from_this());
            }
            connection_closer_->MarkConnectionCloseRetransmitted(current_time);
        }
    }
}

void BaseConnection::DropPacketsInDrainingState(std::vector<std::shared_ptr<IPacket>>& packets) {
    if (qlog_trace_) {
        for (auto& pkt : packets) {
            common::PacketDroppedData drop_data;
            drop_data.packet_type = pkt->GetHeader()->GetPacketType();
            drop_data.trigger = "draining_state";
            QLOG_PACKET_DROPPED(qlog_trace_, drop_data);
        }
    }
}

bool BaseConnection::DispatchByType(const std::shared_ptr<IPacket>& packet) {
    auto packet_type = packet->GetHeader()->GetPacketType();
    switch (packet_type) {
        case PacketType::kNegotiationPacketType:
            return OnVersionNegotiationPacket(packet);
        case PacketType::kInitialPacketType:
            return OnInitialPacket(packet);
        case PacketType::k0RttPacketType:
            return On0rttPacket(packet);
        case PacketType::kHandshakePacketType:
            return OnHandshakePacket(packet);
        case PacketType::kRetryPacketType:
            return OnRetryPacket(packet);
        case PacketType::k1RttPacketType:
            return On1rttPacket(packet);
        default:
            LOG_ERROR("unknown packet type. type:%d", packet_type);
            return false;
    }
}

bool BaseConnection::OnInitialPacket(const std::shared_ptr<IPacket>& packet) {
    LongHeader* header = (LongHeader*)packet->GetHeader();
    uint32_t pkt_version = header->GetVersion();

    // RFC 9368: Record the version the peer used in its FIRST Initial. This
    // is needed on the server side for the mandatory "peer_chosen_version
    // matches on-wire version" consistency check in
    // ValidateAndMaybeUpgradeByRemoteTP().
    if (pkt_version != 0 && version_ctx_.original_version == 0) {
        version_ctx_.original_version = pkt_version;
    }

    if (!connection_crypto_.InitIsReady()) {
        // First Initial on this connection (server side first packet, or
        // client side prior to having installed keys). The DCID in the
        // packet is the one we use to derive the Initial secret.
        if (pkt_version != 0 && pkt_version != version_ctx_.quic_version) {
            LOG_INFO("Updating connection version from packet: 0x%08x -> 0x%08x",
                version_ctx_.quic_version, pkt_version);
            SetVersion(pkt_version);
            // RFC 9368 §4: our version_information TP must advertise
            // chosen_version == current on-wire version. Re-encode and hand
            // the fresh TP buffer to TLS before it serializes
            // EncryptedExtensions.
            RebuildAndPushVersionInformation();
        }

        LOG_INFO("Installing Initial Secret for decryption from packet DCID: length=%u, version=0x%08x",
            header->GetDestinationConnectionIdLength(), version_ctx_.quic_version);
        connection_crypto_.InstallInitSecret(
            (uint8_t*)header->GetDestinationConnectionId(), header->GetDestinationConnectionIdLength(), true);

    } else if (!version_ctx_.is_server && pkt_version != 0 && pkt_version != version_ctx_.quic_version) {
        // RFC 9368 Compatible Version Negotiation (client side):
        // The server decided to upgrade the connection to a different
        // (compatible) QUIC version. We already have an Initial cryptographer
        // installed under the old version's salt, so we cannot decrypt this
        // Initial yet. Re-derive the Initial secret using:
        //   - the new version's salt / labels, and
        //   - the SAME DCID we used for our first Initial
        //     (i.e. the DCID stashed away in ConnectionCrypto on
        //     InstallInitSecret, which is what the server also used when it
        //     rekeyed via ValidateAndMaybeUpgradeByRemoteTP).
        LOG_INFO(
            "RFC 9368: client detected server version upgrade: 0x%08x -> 0x%08x",
            version_ctx_.quic_version, pkt_version);

        const std::string& dcid = connection_crypto_.GetInitialSecretDcid();
        if (dcid.empty()) {
            LOG_ERROR(
                "RFC 9368: cannot rekey client Initial (no cached DCID); dropping packet");
            return false;
        }

        if (!connection_crypto_.RekeyInitialForVersion(
                pkt_version,
                reinterpret_cast<const uint8_t*>(dcid.data()),
                static_cast<uint32_t>(dcid.size()),
                false /* is_server */)) {
            LOG_ERROR("RFC 9368: client-side RekeyInitialForVersion failed");
            return false;
        }

        // Sync connection-level version with crypto-level version.
        SetVersion(pkt_version);

        // Update our version_information TP so that any remaining outbound
        // TLS messages (rare: should already be serialized on the client) see
        // chosen_version == on-wire version. Also keep the cached copy fresh
        // for any future consistency check.
        RebuildAndPushVersionInformation();

        version_ctx_.compat_vn_completed = true;
        common::Metrics::CounterInc(common::MetricsStd::VersionNegotiationTotal);
    }

    return OnNormalPacket(packet);
}

bool BaseConnection::On0rttPacket(const std::shared_ptr<IPacket>& packet) {
    // Handle 0-RTT packet like normal packet using early-data keys if available
    // If early data is disabled on server, the keys won't be available and decryption will fail
    // This is expected behavior - the packet will be dropped and early data will be rejected during TLS handshake
    return OnNormalPacket(packet);
}

bool BaseConnection::On1rttPacket(const std::shared_ptr<IPacket>& packet) {
    // RFC 9001 §6: Set expected key phase for Key Update detection
    auto rtt1_pkt = std::dynamic_pointer_cast<Rtt1Packet>(packet);
    if (rtt1_pkt) {
        rtt1_pkt->SetExpectedKeyPhase(connection_crypto_.GetCurrentKeyPhase());
    }

    // Try normal decrypt path
    if (OnNormalPacket(packet)) {
        return true;
    }

    // Check if decrypt failure was due to Key Phase change (passive Key Update)
    if (rtt1_pkt && packet->IsKeyPhaseChanged() && connection_crypto_.CanKeyUpdate()) {
        LOG_INFO("Detected peer Key Update, triggering passive key rotation");

        // Trigger read (and write) key update
        if (!connection_crypto_.TriggerReadKeyUpdate()) {
            LOG_ERROR("Failed to trigger passive key update");
            return false;
        }

        // Retry payload decryption only (header already decrypted, PN already recovered)
        if (!rtt1_pkt->RetryPayloadDecrypt()) {
            LOG_ERROR("decrypt still failed after key update");
            return false;
        }

        // Log packet_received event to qlog
        if (qlog_trace_) {
            common::PacketReceivedData data;
            data.packet_number = packet->GetPacketNumber();
            data.packet_type = packet->GetHeader()->GetPacketType();
            data.packet_size = packet->GetSrcBuffer().GetLength();
            auto& frames = packet->GetFrames();
            data.frame_objects.reserve(frames.size());
            for (const auto& frame : frames) {
                data.frame_objects.push_back(frame);
            }
            QLOG_PACKET_RECEIVED(qlog_trace_, data);
        }

        if (!OnFrames(packet->GetFrames(), packet->GetCryptoLevel())) {
            LOG_ERROR("process frames failed after key update.");
            return false;
        }

        LOG_INFO("Successfully decrypted packet after passive Key Update, pn:%llu", packet->GetPacketNumber());
        return true;
    }

    return false;
}

bool BaseConnection::OnVersionNegotiationPacket(const std::shared_ptr<IPacket>& packet) {
    auto vn_packet = std::dynamic_pointer_cast<VersionNegotiationPacket>(packet);
    if (!vn_packet) {
        LOG_ERROR("Failed to cast to VersionNegotiationPacket");
        return false;
    }

    auto supported_versions = vn_packet->GetSupportVersion();
    LOG_WARN("Received Version Negotiation packet with %zu supported versions", supported_versions.size());

    // RFC 9000 Section 6.2: Discard if our version is listed (downgrade attack)
    if (IsVnDowngradeAttack(supported_versions)) {
        return true;
    }

    // RFC 9000 Section 6: Version negotiation should only happen once
    if (version_ctx_.version_negotiation_done) {
        LOG_ERROR("Received Version Negotiation packet after already negotiating version - closing connection");
        InnerConnectionClose(QuicErrorCode::kProtocolViolation, 0, "Version negotiation attempted multiple times");
        return true;
    }

    // Select a compatible version and trigger callback
    uint32_t compatible_version = SelectVersion(supported_versions);
    HandleCompatibleVersionFound(compatible_version);

    common::Metrics::CounterInc(common::MetricsStd::VersionNegotiationTotal);
    return true;
}

bool BaseConnection::IsVnDowngradeAttack(const std::vector<uint32_t>& supported_versions) {
    uint32_t our_version = version_ctx_.quic_version;
    for (auto version : supported_versions) {
        if (version == our_version) {
            LOG_WARN("Version Negotiation lists our current version 0x%08x - possible attack!", our_version);
            if (qlog_trace_) {
                common::PacketDroppedData drop_data;
                drop_data.packet_type = PacketType::kNegotiationPacketType;
                drop_data.trigger = "version_negotiation_downgrade";
                QLOG_PACKET_DROPPED(qlog_trace_, drop_data);
            }
            return true;
        }
    }
    return false;
}

void BaseConnection::HandleCompatibleVersionFound(uint32_t compatible_version) {
    uint32_t our_version = version_ctx_.quic_version;
    if (compatible_version != 0 && compatible_version != our_version) {
        LOG_INFO("Found compatible version: 0x%08x (%s), will reconnect", compatible_version,
            VersionToString(compatible_version));
        version_ctx_.negotiated_version = compatible_version;
        version_ctx_.version_negotiation_needed = true;

        if (version_negotiation_cb_) {
            version_negotiation_cb_(compatible_version);
        } else {
            LOG_WARN("Version negotiation callback not set, closing connection");
            InnerConnectionClose(
                QuicErrorCode::kVersionNegotiationError, 0, "Version negotiation required but no handler");
        }
    } else {
        LOG_ERROR("No compatible QUIC version found in server's list");
        InnerConnectionClose(QuicErrorCode::kVersionNegotiationError, 0, "No compatible QUIC version");
    }
}

bool BaseConnection::OnNormalPacket(const std::shared_ptr<IPacket>& packet) {
    std::shared_ptr<ICryptographer> cryptographer = connection_crypto_.GetCryptographer(packet->GetCryptoLevel());
    if (!cryptographer) {
        LOG_ERROR("decrypt grapher is not ready.");
        if (qlog_trace_) {
            common::PacketDroppedData drop_data;
            drop_data.packet_type = packet->GetHeader()->GetPacketType();
            drop_data.packet_size = packet->GetSrcBuffer().GetLength();
            drop_data.trigger = "key_unavailable";
            QLOG_PACKET_DROPPED(qlog_trace_, drop_data);
        }
        return false;
    }

    packet->SetCryptographer(cryptographer);

    // RFC 9000 Appendix A: Set the largest received PN for packet number recovery
    // The full PN is recovered from the truncated encoding using the largest PN
    // successfully received so far in the same packet number space.
    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    packet->SetLargestReceivedPn(recv_control_.GetLargestReceivedPn(ns));

    // PERF: previously this path allocated a pooled BufferChunk + SingleBlockBuffer
    // here and passed it as `out_plaintext` to DecodeWithCrypto. Site-attribution
    // measurements (50MB download, 40K data packets) showed this site at
    // ~172K ctors (4.3 per recv data packet). All four DecodeWithCrypto
    // implementations (Init/Handshake/Rtt0/Rtt1) ignore the parameter and
    // allocate their own plaintext chunk internally, so the buffer built here
    // is unused and immediately dropped. Pass nullptr to skip the wasted alloc.
    // The callee-side allocation (rtt_1_packet.cpp:171 et al.) still happens;
    // removing it requires propagating the plaintext chunk back to the caller
    // and is tracked as a separate step.
    if (!packet->DecodeWithCrypto(nullptr)) {
        LOG_ERROR("decode packet after decrypt failed.");
        if (qlog_trace_) {
            common::PacketDroppedData drop_data;
            drop_data.packet_type = packet->GetHeader()->GetPacketType();
            drop_data.packet_size = packet->GetSrcBuffer().GetLength();
            drop_data.trigger = "decryption_failed";
            QLOG_PACKET_DROPPED(qlog_trace_, drop_data);
        }
        return false;
    }

    // Log packet_received event to qlog
    if (qlog_trace_) {
        common::PacketReceivedData data;
        data.packet_number = packet->GetPacketNumber();
        data.packet_type = packet->GetHeader()->GetPacketType();
        data.packet_size = packet->GetSrcBuffer().GetLength();

        // Pass the full frame objects so the serializer can emit per-frame
        // qlog fields (stream_id/offset, ack ranges, etc.) instead of just
        // a "frame_type" enum.
        auto& frames = packet->GetFrames();
        data.frame_objects.reserve(frames.size());
        for (const auto& frame : frames) {
            data.frame_objects.push_back(frame);
        }

        QLOG_PACKET_RECEIVED(qlog_trace_, data);
    }

    if (!OnFrames(packet->GetFrames(), packet->GetCryptoLevel())) {
        LOG_ERROR("process frames failed.");
        return false;
    }
    return true;
}

bool BaseConnection::OnHandshakePacket(const std::shared_ptr<IPacket>& packet) {
    return OnNormalPacket(packet);
}

bool BaseConnection::OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level) {
    // Metrics: Frames received
    common::Metrics::CounterInc(common::MetricsStd::FramesRxTotal, frames.size());

    // Update last communicate time for PING frames
    for (size_t i = 0; i < frames.size(); i++) {
        uint16_t type = frames[i]->GetType();
        LOG_DEBUG("recv frame: %s", FrameType2String(type).c_str());
        if (type == FrameType::kPing) {
            last_communicate_time_ = common::UTCTimeMsec();
        }
        // Metrics: Connection-level flow control blocked
        if (type == FrameType::kDataBlocked) {
            common::Metrics::CounterInc(common::MetricsStd::QuicFlowControlBlocked);
        }
    }

    // Delegate to frame processor
    return frame_processor_->OnFrames(frames, crypto_level);
}

void BaseConnection::OnTransportParams(TransportParam& remote_tp) {
    // RFC 9368 §4: Validate remote peer's version_information (if present) and,
    // on the server, decide whether to compatibly upgrade the connection version.
    // Must run BEFORE Merge(), because Merge() overwrites our local
    // version_information fields with the peer's values.
    if (!ValidateAndMaybeUpgradeByRemoteTP(remote_tp)) {
        // Downgrade / protocol violation detected; connection has been closed.
        return;
    }

    // RecvFlowController was already initialized with local values during Init().
    // Merge() now does NOT notify listeners, so RecvFlowController retains correct local limits.
    transport_param_.Merge(remote_tp);

    // Explicitly update controllers that need remote transport parameters:
    // - SendFlowController: uses remote max_data/max_streams (peer's limits on what we can send)
    // - RecvControl/SendManager: use remote ack_delay_exponent and max_ack_delay
    send_flow_controller_.UpdateConfig(remote_tp);
    recv_control_.UpdateConfig(remote_tp);
    send_manager_.UpdateConfig(remote_tp);

    // Remember remote transport params for 0-RTT session caching (RFC 9000 Section 7.4.1)
    remote_tp_snapshot_ = RemoteTransportParamSnapshot::From(remote_tp);

    // Update peer's active connection ID limit in coordinator
    cid_coordinator_->SetPeerActiveConnectionIDLimit(remote_tp.GetActiveConnectionIdLimit());
    // Start idle timeout timer through coordinator
    timer_coordinator_->StartIdleTimer([this]() { OnIdleTimeout(); });

    // Preferred Address Migration (RFC 9000 Section 9.6)
    //
    // IMPORTANT: This is CLIENT-SIDE ONLY logic for handling server's preferred address.
    //
    // How it works:
    // 1. SERVER: Advertises a preferred_address in transport parameters during handshake
    //    - This is typically used when the server wants the client to use a different address
    //    - Example: Load balancer forwards initial connection, server wants client to connect directly
    //    - Server sets this via transport_param.SetPreferredAddress("ip:port") before handshake
    //
    // 2. CLIENT: Receives preferred_address and decides whether to migrate (this code)
    //    - Only if active migration is not disabled
    //    - Only if the preferred address is different from current peer address
    //    - Initiates path validation to the new address
    //    - If validation succeeds, switches to the new address
    //
    // 3. SERVER: Does NOT actively migrate its own address
    //    - Server continues listening on all its addresses
    //    - Server responds to PATH_CHALLENGE from client on the preferred address
    //    - After client validates, communication happens on the new address
    //
    if (!transport_param_.GetDisableActiveMigration()) {
        const auto& pref = transport_param_.GetPreferredAddress();
        if (!pref.empty()) {
            LOG_INFO("Server advertised preferred address: %s", pref.c_str());

            // Parse "ip:port" format
            auto pos = pref.find(':');
            if (pos != std::string::npos) {
                common::Address addr(pref.substr(0, pos), static_cast<uint16_t>(std::stoi(pref.substr(pos + 1))));
                if (!(addr == GetPeerAddress())) {
                    LOG_INFO("Client initiating migration to server's preferred address: %s:%d",
                        addr.GetIp().c_str(), addr.GetPort());
                    path_manager_->OnObservedPeerAddress(addr);
                } else {
                    LOG_DEBUG("Preferred address is same as current address, no migration needed");
                }
            } else {
                LOG_WARN("Invalid preferred address format: %s (expected ip:port)", pref.c_str());
            }
        }
    }

    // Initialize local CID pool for potential path migrations
    CheckAndReplenishLocalCIDPool();

    // Start any deferred path probes now that Application keys should be ready
    // (OnTransportParams is called after handshake completes)
    path_manager_->StartNextPathProbe();
}

void BaseConnection::ThreadTransferBefore() {
    // remove idle timeout timer task from old timer (delegated to coordinator)
    timer_coordinator_->OnThreadTransferBefore();
}

void BaseConnection::ThreadTransferAfter() {
    // add idle timeout timer task to new timer (delegated to coordinator)
    timer_coordinator_->OnThreadTransferAfter();
}

void BaseConnection::OnIdleTimeout() {
    // Metrics: Idle timeout
    common::Metrics::CounterInc(common::MetricsStd::IdleTimeoutTotal);

    InnerConnectionClose(QuicErrorCode::kNoError, 0, "idle timeout.");
}

void BaseConnection::OnClosingTimeout() {
    state_machine_.OnCloseTimeout();
}

// RFC 9002: Check for idle timeout from excessive PTOs
void BaseConnection::CheckPTOTimeout() {
    // Only check in Connected state to avoid closing during handshake
    if (!state_machine_.CanSendData()) {
        return;
    }

    uint32_t consecutive_ptos = send_manager_.GetRttCalculator().GetConsecutivePTOCount();

    // RFC 9002: Close connection after persistent timeout (~3 PTO cycles)
    if (consecutive_ptos >= RttCalculator::kMaxConsecutivePTOs) {
        LOG_WARN(
            "Connection idle timeout: %u consecutive PTOs without ACK, closing connection", consecutive_ptos);

        // Metrics: PTO count
        common::Metrics::CounterInc(common::MetricsStd::PtoCountTotal);

        // Close with no error (idle timeout is normal termination)
        InnerConnectionClose(QuicErrorCode::kNoError, 0, "Persistent PTO timeout");
    }
}

void BaseConnection::ToSendFrame(std::shared_ptr<IFrame> frame) {
    send_manager_.ToSendFrame(frame);
    ActiveSend();
}

void BaseConnection::ActiveSendStream(std::shared_ptr<IStream> stream) {
    if (state_machine_.IsTerminating()) {
        return;
    }
    // Guard against accessing stream_manager_ after destruction
    if (!stream_manager_) {
        return;
    }
    if (stream->GetStreamID() != 0) {
        has_app_send_pending_ = true;
        // Notify scheduler that early data (0-RTT) might be needed
        encryption_scheduler_->SetEarlyDataPending(true);
    }
    // Use StreamManager for stream scheduling (Week 4 refactoring)
    stream_manager_->MarkStreamActive(stream);
    ActiveSend();
}

EncryptionLevel BaseConnection::GetCurEncryptionLevel() {
    auto level = connection_crypto_.GetCurEncryptionLevel();

    // In 0-RTT scenario, we need to ensure proper packet sending order:
    // 1. First send Initial packet (with ClientHello)
    // 2. Then send 0-RTT packet (with early data)
    if (has_app_send_pending_ && level == kInitial) {
        // Check if we have 0-RTT keys available
        if (connection_crypto_.GetCryptographer(kEarlyData)) {
            // Check if we have already sent the Initial packet with ClientHello
            // This ensures we don't skip the Initial packet in 0-RTT scenarios
            if (initial_packet_sent_) {
                return kEarlyData;
            } else {
                // Still need to send Initial packet first
                return kInitial;
            }
        }
    }
    return level;
}

void BaseConnection::OnObservedPeerAddress(const common::Address& addr) {
    if (path_manager_) {
        path_manager_->OnObservedPeerAddress(addr);
    }
}

void BaseConnection::ActiveSend() {
    common::Metrics::CounterInc(common::MetricsStd::DiagActiveSendCalls);
    // Don't trigger send retry if connection is closing, draining, or closed
    // This prevents unnecessary retransmissions when connection is terminating
    if (state_machine_.IsTerminating()) {
        LOG_DEBUG("ActiveSend called but connection is terminating, ignoring, state=%d",
            static_cast<int>(state_machine_.GetState()));
        return;
    }

    // NOTE: ActiveSend is on the per-send hot path (one call per outbound
    // packet, one per ACK delivery, one per stream wakeup, one per timer
    // fire). At INFO level under a 25k-request load it produces ~30k log
    // lines/second per worker, all funneled through the synchronous file
    // logger -- the worker thread blocks on disk IO and visibly "freezes"
    // for 5-10 seconds in the middle of a benchmark. Keep at DEBUG.
    if (active_connection_cb_) {
        LOG_DEBUG("ActiveSend: invoking active_connection_cb_");
        active_connection_cb_(shared_from_this());
    } else {
        LOG_WARN("ActiveSend: active_connection_cb_ is null!");
    }
}

// ==================== IConnectionEventSink Implementation ====================
// These methods replace callback-based event notification with direct method calls,
// reducing std::bind overhead and improving performance.

void BaseConnection::OnStreamDataReady(std::shared_ptr<IStream> stream) {
    // Delegate to existing ActiveSendStream method
    ActiveSendStream(stream);
}

void BaseConnection::OnFrameReady(std::shared_ptr<IFrame> frame) {
    // Delegate to existing ToSendFrame method
    ToSendFrame(frame);
}

void BaseConnection::OnConnectionActive() {
    // Delegate to existing ActiveSend method
    ActiveSend();
}

void BaseConnection::OnStreamClosed(uint64_t stream_id) {
    // Delegate to existing InnerStreamClose method
    InnerStreamClose(stream_id);
}

void BaseConnection::OnConnectionClose(uint64_t error, uint16_t frame_type, const std::string& reason) {
    // Delegate to existing InnerConnectionClose method
    InnerConnectionClose(error, frame_type, reason);
}

// ==================== End of IConnectionEventSink Implementation ====================

// Immediate send for critical frames (ACK, PATH_CHALLENGE/RESPONSE, CONNECTION_CLOSE)
// Bypasses normal send path for low latency
bool BaseConnection::SendImmediate(std::shared_ptr<common::IBuffer> buffer) {
    if (!buffer || buffer->GetDataLength() == 0) {
        LOG_WARN("SendImmediate: empty buffer");
        return false;
    }

    // Prefer sender_ (direct UDP send) if available
    if (sender_) {
        auto net_packet = std::make_shared<NetPacket>();
        net_packet->SetData(buffer);
        // During migration, use migration socket for sending
        int32_t send_sock = (migration_sockfd_ > 0) ? migration_sockfd_ : sockfd_;
        net_packet->SetSocket(send_sock);
        net_packet->SetAddress(AcquireSendAddress());
        net_packet->SetTime(common::UTCTimeMsec());

        bool result = sender_->Send(net_packet);
        if (result) {
            LOG_DEBUG("SendImmediate: packet sent via sender_, size=%d, sock=%d", buffer->GetDataLength(), send_sock);
        } else {
            LOG_ERROR("SendImmediate: sender_->Send() failed");
        }
        return result;

    } else {
        LOG_ERROR("SendImmediate: no sender_ available");
        return false;
    }
}

void BaseConnection::InnerConnectionClose(uint64_t error, uint16_t trigger_frame, std::string reason) {
    if (error != QuicErrorCode::kNoError) {
        // Metrics: Error statistics
        switch (error) {
            case QuicErrorCode::kFlowControlError:
                common::Metrics::CounterInc(common::MetricsStd::ErrorsFlowControl);
                break;
            case QuicErrorCode::kStreamLimitError:
                common::Metrics::CounterInc(common::MetricsStd::ErrorsStreamLimit);
                break;
            case QuicErrorCode::kProtocolViolation:
            case QuicErrorCode::kFrameEncodingError:
            case QuicErrorCode::kTransportParameterError:
            case QuicErrorCode::kConnectionIdLimitError:
                common::Metrics::CounterInc(common::MetricsStd::ErrorsProtocol);
                break;
            case QuicErrorCode::kInternalError:
                common::Metrics::CounterInc(common::MetricsStd::ErrorsInternal);
                break;
            default:
                break;
        }

        ImmediateClose(error, trigger_frame, reason);

    } else {
        Close();
    }
}

void BaseConnection::ImmediateClose(uint64_t error, uint16_t trigger_frame, std::string reason) {
    if (!state_machine_.CanSendData()) {
        return;
    }

    // Cancel all streams (delegated to stream manager)
    stream_manager_->ResetAllStreams(error);

    // Delegate to connection closer
    connection_closer_->StartImmediateClose(error, trigger_frame, reason, [this]() { ActiveSend(); });
}

void BaseConnection::InnerStreamClose(uint64_t stream_id) {
    // Check if stream exists before closing (for metrics)
    auto stream = stream_manager_->FindStream(stream_id);
    if (stream) {
        // Delegate to stream manager
        stream_manager_->CloseStream(stream_id);

        // Metrics: Stream closed
        common::Metrics::GaugeDec(common::MetricsStd::QuicStreamsActive);
        common::Metrics::CounterInc(common::MetricsStd::QuicStreamsClosed);
    }
}

void BaseConnection::OnStreamDataAcked(uint64_t stream_id, uint64_t offset_start, uint64_t length, bool has_fin) {
    // Delegate to stream manager
    stream_manager_->OnStreamDataAcked(stream_id, offset_start, length, has_fin);
}

void BaseConnection::AddConnectionId(ConnectionID& id) {
    if (add_conn_id_cb_) {
        add_conn_id_cb_(id, shared_from_this());
    }
}

void BaseConnection::RetireConnectionId(ConnectionID& id) {
    if (retire_conn_id_cb_) {
        retire_conn_id_cb_(id);
    }
}

void BaseConnection::CheckAndReplenishLocalCIDPool() {
    // Delegate to ConnectionIDCoordinator
    cid_coordinator_->CheckAndReplenishLocalCIDPool();
}

bool BaseConnection::InitiateMigration() {
    // RFC 9000 Section 9: Connection Migration (Simple API for interop tests)
    // This is a convenience wrapper that delegates to the production API.
    // It keeps the same local IP but gets a new ephemeral port from the system.

    LOG_INFO("InitiateMigration: delegating to production API InitiateMigrationTo()");

    // Get current local address
    std::string current_ip;
    uint32_t current_port;
    GetLocalAddr(current_ip, current_port);

    if (current_ip.empty() || current_ip == "::") {
        // Dual-stack socket reports "::" as local address. For migration, we need
        // to match the address family of the peer to ensure the new socket can
        // communicate with the peer. IPv4 peers need an IPv4 socket.
        bool peer_is_ipv4 = (peer_addr_.GetIp().find(':') == std::string::npos);
        if (peer_is_ipv4) {
            current_ip = "0.0.0.0";
            LOG_INFO("InitiateMigration: peer is IPv4, using 0.0.0.0 for migration");
        } else {
            current_ip = "::";
            LOG_INFO("InitiateMigration: peer is IPv6, using :: for migration");
        }
    }

    // Delegate to production API: same IP, but port=0 means system chooses new port
    // This creates a real socket switch, which is what production migration does
    MigrationResult result = InitiateMigrationTo(current_ip, 0);

    bool success = (result == MigrationResult::kSuccess);
    if (!success) {
        LOG_WARN("InitiateMigration: failed with result %d", static_cast<int>(result));
    }

    return success;
}

MigrationResult BaseConnection::InitiateMigrationTo(const std::string& local_ip, uint16_t local_port) {
    // RFC 9000 Section 9: Connection Migration (Production API)
    // This implements full client-initiated connection migration with local address change

    LOG_INFO("BaseConnection::InitiateMigrationTo: starting migration to %s:%d", local_ip.c_str(), local_port);

    // 1. Check if connection is in a state that allows migration
    if (!state_machine_.CanSendData()) {
        LOG_WARN("InitiateMigrationTo: connection not in connected state");
        return MigrationResult::kFailedInvalidState;
    }

    // 2. Setup callbacks for socket management
    if (path_manager_) {
        path_manager_->SetSocketCallbacks(
            [this]() { return sockfd_; }, [this](int32_t sock) { migration_sockfd_ = sock; });

        // Set migration complete callback to handle socket switch
        path_manager_->SetMigrationCompleteCallback([this](const MigrationInfo& info) { OnMigrationComplete(info); });
    }

    // 3. Create address and delegate to PathManager
    common::Address local_addr(local_ip, local_port);

    if (!path_manager_) {
        return MigrationResult::kFailedInvalidState;
    }

    auto result = path_manager_->InitiateMigrationToAddress(local_addr);

    // 4. Register migration socket with receiver so PATH_RESPONSE can be received
    if (result == MigrationResult::kSuccess && migration_sockfd_ > 0 && register_socket_cb_) {
        if (!register_socket_cb_(migration_sockfd_)) {
            LOG_ERROR("InitiateMigrationTo: failed to register migration socket %d with receiver",
                migration_sockfd_);
        } else {
            LOG_INFO("InitiateMigrationTo: registered migration socket %d with receiver",
                migration_sockfd_);
        }
    }

    return result;
}

void BaseConnection::SetMigrationCallback(migration_callback cb) {
    migration_cb_ = cb;
}

void BaseConnection::GetLocalAddr(std::string& addr, uint32_t& port) {
    // Use IConnection's implementation which queries from socket
    IConnection::GetLocalAddr(addr, port);
}

bool BaseConnection::IsMigrationSupported() const {
    return !transport_param_.GetDisableActiveMigration();
}

bool BaseConnection::IsMigrationInProgress() const {
    return path_manager_ && path_manager_->IsPathProbeInflight();
}

void BaseConnection::OnMigrationComplete(const MigrationInfo& info) {
    LOG_INFO("BaseConnection::OnMigrationComplete: result=%d, is_nat_rebinding=%d",
        static_cast<int>(info.result_), info.is_nat_rebinding_);

    if (info.result_ == MigrationResult::kSuccess) {
        // Migration successful: switch to the new socket
        if (migration_sockfd_ > 0) {
            int32_t old_sock = sockfd_;
            sockfd_ = migration_sockfd_;
            migration_sockfd_ = -1;

            // Update cached local address
            common::Address new_local;
            if (GetLocalAddressFromSocket(sockfd_, new_local)) {
                local_addr_ = new_local;
            }

            LOG_INFO("BaseConnection: switched to migration socket %d (old: %d)", sockfd_, old_sock);

            // Note: The old socket might still be in use by the event loop
            // We don't close it here - the caller (Worker) should manage socket lifecycle
        }
    } else {
        // Migration failed: cleanup migration socket if any
        if (migration_sockfd_ > 0) {
            common::Close(migration_sockfd_);
            migration_sockfd_ = -1;
        }
    }

    // Notify application layer
    if (migration_cb_) {
        // Need to cast shared_from_this() to IQuicConnection
        auto self = std::dynamic_pointer_cast<IQuicConnection>(shared_from_this());
        migration_cb_(self, info);
    }
}

void BaseConnection::OnStateToConnected() {
    // Log connection_state_updated event to qlog
    if (qlog_trace_) {
        common::ConnectionStateUpdatedData data;
        data.old_state = "handshake";
        data.new_state = "connected";

        auto event_data = std::make_unique<common::ConnectionStateUpdatedData>(data);
        QLOG_EVENT(qlog_trace_, common::QlogEvents::kConnectionStateUpdated, std::move(event_data));
    }

    // Metrics: Calculate and record handshake duration.
    // Both endpoints are wall-clock (UTCTimeMsec); the subtraction is expressed
    // in microseconds for the gauge. A backwards wall-clock jump between start
    // and now would underflow the unsigned subtraction, so we guard it.
    if (handshake_start_wall_time_ms_ > 0) {
        const uint64_t now_ms = common::UTCTimeMsec();
        if (now_ms >= handshake_start_wall_time_ms_) {
            uint64_t duration_us = (now_ms - handshake_start_wall_time_ms_) * 1000;
            common::Metrics::GaugeSet(common::MetricsStd::QuicHandshakeDurationUs, duration_us);
            LOG_DEBUG("Handshake completed in %llu microseconds", duration_us);
        } else {
            LOG_DEBUG("Skipping handshake duration metric: wall clock moved backwards");
        }
    }
}

void BaseConnection::OnStateToClosing() {
    // Log connection_state_updated event to qlog
    if (qlog_trace_) {
        common::ConnectionStateUpdatedData data;
        data.old_state = "connected";  // Could be "handshake" if closing early
        data.new_state = "closing";

        auto event_data = std::make_unique<common::ConnectionStateUpdatedData>(data);
        QLOG_EVENT(qlog_trace_, common::QlogEvents::kConnectionStateUpdated, std::move(event_data));
    }

    // Pre-emptive idle-timer cleanup. We are guaranteed to be on the loop
    // thread here (state transitions are driven from packet ingress / the
    // idle-timer fire itself / loop-thread timers), so RemoveTimer is safe.
    // Doing it now means ~TimerCoordinator (which may run on a different
    // thread when the connection is dropped during teardown) finds
    // idle_timer_active_=false and never touches the EventLoop. This closes
    // the cross-thread fatal observed ~750 ms after CloseInternal in
    // interop runs.
    timer_coordinator_->StopIdleTimer();

    send_manager_.ClearRetransmissionData();
    send_manager_.ClearActiveStreams();
    send_manager_.wait_frame_list_.clear();

    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(connection_closer_->GetClosingErrorCode());
    frame->SetErrFrameType(connection_closer_->GetClosingTriggerFrame());
    frame->SetReason(connection_closer_->GetClosingReason());

    // Add CONNECTION_CLOSE frame to send queue
    send_manager_.ToSendFrame(frame);

    // Trigger active connection callback to send CONNECTION_CLOSE frame
    // Note: We need to explicitly trigger sending because ActiveSend() is blocked in Closing state
    if (active_connection_cb_) {
        LOG_DEBUG("Triggering active connection callback to send CONNECTION_CLOSE frame");
        active_connection_cb_(shared_from_this());
    }

    // Record the time when CONNECTION_CLOSE is first sent
    // RFC 9000 Section 10.2: Retransmit at most once per PTO to avoid flooding
    // This ensures we don't retransmit too frequently when receiving packets
    connection_closer_->MarkConnectionCloseRetransmitted(common::UTCTimeMsec());

    // Immediately notify application layer when entering Closing state.
    // The QUIC layer still needs to wait 3×PTO for CONNECTION_CLOSE retransmission
    // and peer ACKs per RFC 9000, but the application (HTTP/3 client, user code) must
    // be able to release resources and proceed without waiting that long. The callback
    // is idempotent (guarded by connection_close_cb_invoked_), so this is safe even
    // when OnStateToClosed later fires for the same connection.
    //
    // IMPORTANT: Grab a temporary shared_from_this() BEFORE InvokeConnectionCloseCallback.
    // The callback may synchronously drop the last external strong reference
    // (Worker::HandleConnectionClose erases us from conn_map_), which would
    // destroy `this` and invalidate event_loop_. The local `self` keeps us
    // alive through the rest of this method. The timer callback uses weak_ptr
    // to avoid an EventLoop→Connection cycle.
    auto self = shared_from_this();
    connection_closer_->InvokeConnectionCloseCallback(
        self, connection_closer_->GetClosingErrorCode(), connection_closer_->GetClosingReason());

    uint32_t wait_ms = connection_closer_->GetCloseWaitTime() * 3;
    auto loop = event_loop_.lock();
    if (!loop) return;
    auto weak_self = weak_from_this();
    loop->AddTimer(
        [weak_self]() {
            auto self = weak_self.lock();
            if (!self) return;
            self->OnClosingTimeout();
        },
        wait_ms, false);
}

void BaseConnection::OnStateToDraining() {
    // Log connection_state_updated event to qlog
    if (qlog_trace_) {
        common::ConnectionStateUpdatedData data;
        data.old_state = "closing";  // Could be "connected" if peer initiated close
        data.new_state = "draining";

        auto event_data = std::make_unique<common::ConnectionStateUpdatedData>(data);
        QLOG_EVENT(qlog_trace_, common::QlogEvents::kConnectionStateUpdated, std::move(event_data));
    }

    // Pre-emptive idle-timer cleanup (see OnStateToClosing for rationale).
    timer_coordinator_->StopIdleTimer();

    send_manager_.ClearRetransmissionData();
    send_manager_.ClearActiveStreams();
    send_manager_.wait_frame_list_.clear();

    // IMPORTANT: Same self-pinning as OnStateToClosing — grab shared_from_this()
    // BEFORE the callback to keep `this` alive through the timer setup.
    // Timer callback uses weak_ptr to avoid EventLoop→Connection cycle.
    auto self = shared_from_this();
    connection_closer_->InvokeConnectionCloseCallback(
        self, connection_closer_->GetClosingErrorCode(), connection_closer_->GetClosingReason());

    auto loop = event_loop_.lock();
    if (!loop) return;
    auto weak_self = weak_from_this();
    loop->AddTimer(
        [weak_self]() {
            auto self = weak_self.lock();
            if (!self) return;
            self->OnClosingTimeout();
        },
        connection_closer_->GetCloseWaitTime() * 3, false);
}

void BaseConnection::OnStateToClosed() {
    // Log connection_closed event
    if (qlog_trace_) {
        common::ConnectionClosedData data;
        data.error_code = connection_closer_->GetClosingErrorCode();
        data.reason = connection_closer_->GetClosingReason();

        // Determine trigger based on error code
        if (connection_closer_->GetClosingErrorCode() == 0) {
            data.trigger = "clean";
        } else if (connection_closer_->GetClosingTriggerFrame() != 0) {
            data.trigger = "error";
        } else {
            data.trigger = "application";
        }

        QLOG_CONNECTION_CLOSED(qlog_trace_, data);
        qlog_trace_->Flush();  // ensure event is written
    }

    // Stop idle timer through coordinator
    timer_coordinator_->StopIdleTimer();

    // Only invoke callback if it hasn't been called yet
    // (may have been called earlier in OnStateToDraining)
    connection_closer_->InvokeConnectionCloseCallback(shared_from_this(), QuicErrorCode::kNoError, "normal close.");
}

// ==================== New High-Level Send Interfaces Implementation ====================

bool BaseConnection::TrySend() {
    common::Metrics::CounterInc(common::MetricsStd::DiagTrySendIters);
    // 1. State check - allow Connecting, Connected, and Closing states
    // - Connecting: needed for handshake packets
    // - Connected: normal data transmission
    // - Closing: needed to send CONNECTION_CLOSE frames
    // - Draining/Closed: should not send any packets
    if (state_machine_.IsClosed() || state_machine_.IsDraining()) {
        LOG_DEBUG(
            "BaseConnection::TrySend: connection is closed/draining, state=%d", state_machine_.GetState());
        return false;
    }

    // 2. Dispatch: retransmit lost packets first (RFC 9000 §13.3), then
    // fall through to the normal-send path. Both helpers preserve the
    // original return-value contract used by Worker::ProcessSend (true ⇒
    // re-enter TrySend, false ⇒ stop this round).
    if (send_manager_.GetSendControl().NeedReSend()) {
        return TrySendRetransmit();
    }
    return TrySendNew();
}

bool BaseConnection::TrySendRetransmit() {
    // RFC 9000 §13.3: Retransmit lost packets first.
    // QUIC does not retransmit lost packets directly. Instead, the lost packet
    // (which still holds its original payload/frames) is re-encoded with a new
    // packet number and re-encrypted, then sent as a brand-new packet.
    auto& send_control = send_manager_.GetSendControl();
    auto& lost_packets = send_control.GetLostPacket();
    auto lost_entry = lost_packets.front();
    lost_packets.pop_front();
    auto lost_pkt = lost_entry.packet;

    // Determine encryption level and get cryptographer
    auto crypto_level = lost_pkt->GetCryptoLevel();
    auto cryptographer = connection_crypto_.GetCryptographer(crypto_level);
    if (!cryptographer) {
        LOG_WARN("BaseConnection::TrySendRetransmit: no cryptographer for lost packet level=%d, dropping",
            crypto_level);
        return !lost_packets.empty();  // try next lost packet
    }

    // Check congestion window before retransmitting
    uint32_t max_bytes = send_manager_.GetAvailableWindow();
    if (max_bytes == 0) {
        // Put the packet back for later retransmission
        lost_packets.push_front(lost_entry);
        send_manager_.SetCwndLimited();
        return false;
    }

    // Assign new packet number
    auto ns = CryptoLevel2PacketNumberSpace(crypto_level);
    uint64_t new_pn = send_manager_.GetPacketNumber().NextPacketNumber(ns);
    // [DIAG-RTX] Capture the *original* PN before we overwrite it, so the
    // first-send log line can be correlated with this retransmission.
    uint64_t orig_pn = lost_pkt->GetPacketNumber();
    lost_pkt->SetPacketNumber(new_pn);
    lost_pkt->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(new_pn));
    lost_pkt->SetCryptographer(cryptographer);

    // RFC 9001 §6.5: A retransmitted packet MUST be re-encoded with the *current*
    // key phase and *current* cryptographer. Without this synchronization the
    // retransmission could ship plaintext ciphered under the new key while the
    // header advertises the old Key Phase bit (or vice versa), causing the
    // peer's AEAD verification to fail and the packet to be silently dropped.
    // Observed in cross-implementation interop with quic-go/quiche under the
    // ns-3 simulated network: client triggers Key Update at PN ~50, but quicX
    // server keeps replaying lost packets with stale Key Phase bits, peer
    // drops every retransmission, loss detector keeps firing, PN explodes
    // (>130k) and the connection eventually idle-times-out.
    if (lost_pkt->GetHeader()->GetHeaderType() == PacketHeaderType::kShortHeader) {
        lost_pkt->GetHeader()->GetShortHeaderFlag().SetKeyPhase(connection_crypto_.GetCurrentKeyPhase());
    }

    // Re-encode with new packet number and fresh encryption
    auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("BaseConnection::TrySendRetransmit: failed to allocate buffer for retransmission");
        return false;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    // [DIAG-RTX] Snapshot the payload bytes that lost_pkt is about to re-encode.
    // Compare with the matching "first-send pn=<old_pn>" log to determine
    // whether the SharedBufferSpan still points to the original plaintext or
    // whether the underlying chunk has been overwritten / freed by some
    // intermediate path.
    if (auto rtt1 = std::dynamic_pointer_cast<Rtt1Packet>(lost_pkt)) {
        auto pl = rtt1->GetPayload();
        char head[64] = {0};
        uint32_t dump_len = pl.GetLength() < 16 ? pl.GetLength() : 16;
        for (uint32_t i = 0; i < dump_len; ++i) {
            std::snprintf(head + i * 3, sizeof(head) - i * 3, "%02x ",
                pl.Valid() ? pl.GetStart()[i] : 0);
        }
        LOG_INFO("[DIAG-RTX] retransmit-pre orig_pn=%llu new_pn=%llu payload_len=%u "
                 "payload_valid=%d chunk=%p head=%s",
            (unsigned long long)orig_pn, (unsigned long long)new_pn, pl.GetLength(),
            (int)pl.Valid(), (void*)pl.GetChunk().get(), head);
    }

    if (!lost_pkt->Encode(buffer)) {
        LOG_ERROR("BaseConnection::TrySendRetransmit: failed to re-encode lost packet pn=%llu", new_pn);
        return false;
    }

    uint32_t encoded_size = buffer->GetDataLength();

    // Record this retransmission in SendControl carrying the original
    // stream_data, otherwise an ACK on the new PN would not flow back to
    // SendStream::OnDataAcked and the byte-range bookkeeping would be
    // permanently missing the bytes that the retransmit just delivered.
    send_control.OnPacketSend(common::UTCTimeMsec(), lost_pkt, encoded_size, lost_entry.stream_data);

    // DIAGNOSTIC (H-plan): log the stream_data byte ranges actually carried
    // by this retransmitted packet so we can correlate with what the peer
    // observes on the wire. If the peer never sees these byte ranges as
    // duplicates, the retransmission lost its payload.
    std::string sd_summary;
    for (const auto& sd : lost_entry.stream_data) {
        sd_summary += "{sid=" + std::to_string(sd.stream_id) +
                      ",off=" + std::to_string(sd.offset_start) +
                      ",len=" + std::to_string(sd.length) +
                      ",fin=" + std::to_string(sd.has_fin) + "}";
    }
    LOG_INFO("BaseConnection::TrySendRetransmit: retransmitted lost packet with new pn=%llu, size=%u, "
                    "stream_data count=%zu ranges=%s",
        new_pn, encoded_size, lost_entry.stream_data.size(), sd_summary.c_str());

    return SendBuffer(buffer);
}

bool BaseConnection::TrySendNew() {
    // Normal-send branch of TrySend (factored out from the original 363-line
    // monolith). All references to "TrySend" in log strings below were kept as
    // "BaseConnection::TrySend" to avoid noisy log-search churn for ops/tests.

    // 2. Get send context (determine encryption level)
    auto send_ctx = encryption_scheduler_->GetNextSendContext();
    LOG_DEBUG("BaseConnection::TrySend: selected encryption level=%d", send_ctx.level);

    // 3. Get cryptographer
    auto cryptographer = connection_crypto_.GetCryptographer(send_ctx.level);
    if (!cryptographer) {
        LOG_ERROR("BaseConnection::TrySend: no cryptographer for level=%d", send_ctx.level);
        return false;
    }

    // 4. Check congestion window
    uint32_t max_bytes = send_manager_.GetAvailableWindow();
    if (max_bytes == 0) {
        // RFC 9000 §9.3.3: An endpoint sending a probing packet on a path is exempt from
        // amplification limits and any congestion limits that would prevent that packet from
        // being sent. PATH_CHALLENGE / PATH_RESPONSE / NEW_CONNECTION_ID / PADDING are probing
        // frames. If the wait list contains a PATH_CHALLENGE or PATH_RESPONSE, allow sending
        // a probing packet up to MTU to validate the path even when cwnd is full.
        bool has_probing = false;
        for (const auto& f : send_manager_.wait_frame_list_) {
            uint16_t t = static_cast<uint16_t>(f->GetType());
            if (t == FrameType::kPathChallenge || t == FrameType::kPathResponse) {
                has_probing = true;
                break;
            }
        }
        if (has_probing) {
            // Allow up to one MTU's worth of probing data; do not mark cwnd limited.
            // RFC 9000 §14.1 + §8.2.1 floor.
            max_bytes = kMinInitialPacketSize;
            LOG_DEBUG("BaseConnection::TrySend: cwnd full but probing frame pending — bypassing cwnd (RFC 9000 §9.3.3)");
        } else {
            LOG_DEBUG("BaseConnection::TrySend: congestion window full");
            // Mark as cwnd limited so that when ACK arrives, send_retry_cb_ will be called
            send_manager_.SetCwndLimited();
            common::Metrics::CounterInc(common::MetricsStd::DiagTrySendCwndBlocked);
            return false;
        }
    }

    // 5. Get pending frames
    auto frames = send_manager_.GetPendingFrames(send_ctx.level, max_bytes);
    LOG_DEBUG("BaseConnection::TrySend: got %zu pending frames", frames.size());

    // 6. Add pending ACK if needed
    if (send_ctx.has_pending_ack) {
        auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), send_ctx.ack_space, ecn_enabled_);
        if (ack_frame) {
            frames.insert(frames.begin(), ack_frame);
            LOG_DEBUG("BaseConnection::TrySend: added ACK frame for ns=%d", send_ctx.ack_space);
        }
    }

    // 7. Check if there's stream data to send
    bool has_stream_data = send_manager_.HasStreamData(send_ctx.level);
    LOG_DEBUG("BaseConnection::TrySend: has_stream_data=%d", has_stream_data);

    // 8. If no data at all, return
    if (frames.empty() && !has_stream_data) {
        LOG_DEBUG("BaseConnection::TrySend: no data to send");
        common::Metrics::CounterInc(common::MetricsStd::DiagTrySendNoData);
        return false;
    }

    // 9. Build data packet context
    PacketBuilder::DataPacketContext build_ctx;
    build_ctx.level = send_ctx.level;
    build_ctx.cryptographer = cryptographer;
    build_ctx.local_cid_manager = cid_coordinator_->GetLocalConnectionIDManager().get();
    build_ctx.remote_cid_manager = cid_coordinator_->GetRemoteConnectionIDManager().get();
    build_ctx.quic_version = connection_crypto_.GetVersion();
    build_ctx.key_phase = connection_crypto_.GetCurrentKeyPhase();
    build_ctx.frames = std::move(frames);
    build_ctx.stream_manager = stream_manager_.get();
    build_ctx.include_stream_data = has_stream_data;
    build_ctx.add_padding = (send_ctx.level == kInitial);
    build_ctx.min_size = kMinInitialPacketSize;  // RFC 9000 §14.1
    build_ctx.token = send_manager_.GetToken();

    // Set connection-level flow control limit for stream data
    uint64_t conn_flow_limit = 0;
    std::shared_ptr<IFrame> blocked_frame;
    bool fc_blocked_with_data = false;
    if (send_flow_controller_.CanSendData(conn_flow_limit, blocked_frame)) {
        // max_stream_data_size expresses ONLY the connection-level flow-control
        // slack the peer currently grants us (per RFC 9000 §4.1). It must NOT
        // be conflated with cwnd/MTU:
        //   * Per-packet cwnd/pacing is already enforced by the packet builder
        //     when it consumes max_bytes worth of frames.
        //   * Per-datagram MTU is already enforced by FixBufferFrameVisitor's
        //     fixed buffer (kVisitorBudget≈1420B) and by GetPacketLeftSize().
        // Previously this line did `min(max_bytes, conn_flow_limit)`, which let
        // cwnd's per-window byte budget (often capped to MTU≈1450B by
        // SendManager::GetAvailableWindow) leak into the stream-level cap.
        // Result: visitor->GetLeftStreamDataSize() returned ~1234B on average
        // and STREAM payload was permanently throttled to ≤1300B even when
        // the packet buffer had ~1396B of usable room. Drop the max_bytes
        // factor so the visitor's stream cap reflects pure conn FC slack;
        // packet- and cwnd-level limits still bind via their proper paths.
        build_ctx.max_stream_data_size =
            static_cast<uint32_t>(std::min<uint64_t>(conn_flow_limit, UINT32_MAX));
        // DIAGNOSTIC (G2): expose budget components when stream data is queued
        // and yet we keep producing zero-byte STREAM frames. We want to know
        // whether cwnd (max_bytes) or conn-FC slack (conn_flow_limit) is the
        // dominant 0 — and in particular, whether max_stream_data_size is
        // smaller than the per-STREAM frame header overhead (~10–20 bytes)
        // even though the stream has data ready.
        if (has_stream_data && build_ctx.max_stream_data_size < 32) {
            LOG_INFO(
                "BaseConnection::TrySend budget: max_bytes(cwnd)=%u, conn_flow_limit=%llu, "
                "max_stream_data_size=%u (<32) — stream frame header may not fit",
                max_bytes, (unsigned long long)conn_flow_limit, build_ctx.max_stream_data_size);
        }
        // RFC 9000 §19.12: near-limit proactive DATA_BLOCKED still needs to
        // be queued for transmission. CanSendData() returns true (still some
        // headroom) but may have emitted a DATA_BLOCKED on the side; without
        // this branch the frame is allocated and silently dropped.
        if (blocked_frame) {
            LOG_DEBUG(
                "BaseConnection::TrySend: queueing proactive DATA_BLOCKED frame (near limit)");
            send_manager_.ToSendFrame(blocked_frame);
        }
    } else {
        // Flow control blocked, don't send stream data
        build_ctx.max_stream_data_size = 0;
        LOG_DEBUG(
            "BaseConnection::TrySend: connection-level FC blocked, blocked_frame=%p, has_stream_data=%d",
            blocked_frame.get(), has_stream_data ? 1 : 0);
        if (blocked_frame) {
            // Queue the DATA_BLOCKED frame
            send_manager_.ToSendFrame(blocked_frame);
        }
        // Bug #17: when the connection-level flow control wall is reached and
        // we still have streams asking to send (has_stream_data == true), the
        // worker would otherwise see TrySend() ultimately return false (the
        // packet builder will produce "no data to send" because no STREAM
        // frame can be encoded under max_stream_data_size == 0) and remove
        // the connection from its active set. Once removed, no event will
        // re-enter TrySend until the *peer* sends MAX_DATA — but quic-go /
        // quiche only emit MAX_DATA after the application consumes the data,
        // which can be far in the future (or never, if the peer is itself
        // stuck waiting for more bytes from us). Mark the manager as flow-
        // control-blocked so a recheck timer keeps polling, and so the next
        // ACK forces a retry.
        if (has_stream_data) {
            fc_blocked_with_data = true;
        }
    }
    if (fc_blocked_with_data) {
        send_manager_.SetFlowControlBlocked();
    }

    // 10. Build packet - allocate buffer chunk first
    auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("BaseConnection::TrySend: failed to allocate buffer chunk");
        return false;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    // PERF VALIDATION (send-side queueing): time BuildDataPacket and
    // SendBuffer separately. Hypothesis: even though sender_->Send() is a
    // synchronous sendto(), the per-packet build+send cost may be high
    // enough that with N back-to-back packets per worker tick the last
    // packet's wire-time lags the first packet's RecordPacketSendUs() by
    // tens of milliseconds, inflating the apparent RTT. Cheap clock reads
    // (steady_clock) are gated by IsEnabled() to keep the production path
    // unaffected.
    uint64_t t_build_start = common::Metrics::NowUs();

    auto result = packet_builder_->BuildDataPacket(
        build_ctx, buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl());

    {
        uint64_t t_build_end = common::Metrics::NowUs();
        if (t_build_end > t_build_start) {
            common::Metrics::HistogramObserve(
                common::MetricsStd::DiagBuildLatencyUs, t_build_end - t_build_start);
        }
    }

    if (!result.success) {
        LOG_ERROR("BaseConnection::TrySend: failed to build packet: %s "
                          "[max_bytes(cwnd)=%u, conn_flow_limit=%llu, max_stream_data_size=%u, "
                          "frames=%zu, has_stream_data=%d]",
                          result.error_message.c_str(),
                          max_bytes, (unsigned long long)conn_flow_limit,
                          build_ctx.max_stream_data_size,
                          build_ctx.frames.size(), has_stream_data ? 1 : 0);
        // Bug #20 (companion to #17/#19): "no data to send" while
        // has_stream_data was true means stream(s) refused to produce a frame
        // (returned kBreak / kFlowControlBlocked) but no DATA_BLOCKED frame
        // was queued either — neither cwnd-full (handled at line 1740) nor
        // explicit conn-FC-blocked (handled at line 1838). This typically
        // happens when max_bytes (cwnd headroom) is small enough that the
        // STREAM frame header + offset cannot fit, or after a SendStream
        // returns kBreak because conn-level slack was 0. Without a wakeup
        // path the worker erases the connection from active set and we sit
        // silent until idle timeout. Arm the same recheck timer used by
        // Bug #17 so we re-enter TrySend periodically; once cwnd grows back
        // (next ACK) or conn-FC slack widens (next MAX_DATA), the stream
        // can resume.
        if (has_stream_data && !fc_blocked_with_data) {
            send_manager_.SetFlowControlBlocked();
        }
        common::Metrics::CounterInc(common::MetricsStd::DiagTrySendBuildFail);
        return false;
    }

    LOG_DEBUG(
        "BaseConnection::TrySend: built packet pn=%llu, size=%u bytes", result.packet_number, result.packet_size);

    // 11. Mark Initial packet as sent (if needed)
    if (send_ctx.level == kInitial) {
        encryption_scheduler_->SetInitialPacketSent(true);
    }

    // 12. Send buffer
    uint64_t t_send_start = common::Metrics::NowUs();
    bool send_success = SendBuffer(buffer);
    {
        uint64_t t_send_end = common::Metrics::NowUs();
        if (t_send_end > t_send_start) {
            common::Metrics::HistogramObserve(
                common::MetricsStd::DiagSendLatencyUs, t_send_end - t_send_start);
        }
    }

    // PERF VALIDATION: count successful datagram emissions. TrySend hands one
    // datagram to SendBuffer per successful pass, so this is also "successful
    // TrySend iterations on the data path" (the lost-packet retransmit branch
    // earlier returns directly without reaching here — that branch is rare in
    // a clean loopback transfer and intentionally excluded).
    if (send_success) {
        // Packet payload size distribution diagnostic
        common::Metrics::HistogramObserve(
            common::MetricsStd::DiagPktPayloadHist, buffer->GetDataLength());
    }

    // 12.5. Update connection-level send flow control tracking
    // CRITICAL: Without this, sent_bytes_ stays at 0 and flow control is bypassed.
    // Peers with smaller initial_max_data would see FLOW_CONTROL_ERROR.
    if (send_success && result.stream_data_size > 0) {
        send_flow_controller_.OnDataSent(result.stream_data_size);
    }

    // 13. RFC 9001 Section 6: Check if Key Update should be triggered
    if (send_success && send_ctx.level == kApplication && key_update_trigger_.IsEnabled()) {
        if (key_update_trigger_.OnBytesSent(result.packet_size)) {
            // Trigger key update
            if (connection_crypto_.TriggerKeyUpdate()) {
                key_update_trigger_.MarkTriggered();
                key_update_trigger_.Reset();
                LOG_INFO("Key Update triggered after sending %u bytes", result.packet_size);
            }
        }
    }

    return send_success;
}

bool BaseConnection::SendBuffer(std::shared_ptr<common::IBuffer> buffer) {
    if (!buffer || buffer->GetDataLength() == 0) {
        LOG_WARN("BaseConnection::SendBuffer: empty buffer");
        common::Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
        return false;
    }

    // Use sender_ if available (preferred)
    if (sender_) {
        // PERF (P1 follow-up): proactively populate the sockaddr cache on
        // peer_addr_ so the value-typed copy made by AcquireSendAddress()
        // and stored on the NetPacket already has a ready-to-use cache.
        // Without this, every NetPacket Address starts empty and
        // UdpSender::SendBatch's fast-path probe degrades to per-packet
        // sendto for the entire batch (observed udp_sb_ok=0 on a 500MB
        // upload). Both family slots are populated so that v4-on-v6-dual-
        // stack and v6 sockets all hit the cache. EnsureSockaddrCache is
        // idempotent — first call is a single inet_pton; subsequent calls
        // are a single bool check.
        peer_addr_.EnsureSockaddrCache(AF_INET);
        peer_addr_.EnsureSockaddrCache(AF_INET6);

        auto packet = std::make_shared<NetPacket>();
        packet->SetData(buffer);
        packet->SetAddress(AcquireSendAddress());
        // During migration, use migration socket for sending
        int32_t send_sock = (migration_sockfd_ > 0) ? migration_sockfd_ : sockfd_;
        packet->SetSocket(send_sock);

        // PERF (sendmmsg batch path): if Worker installed a sink for this
        // drain round, just hand the packet to it and return. The actual
        // sendmmsg(2) syscall is issued once at the end of the drain over
        // the whole accumulated batch. Order is preserved (push_back is
        // FIFO) and there is no buffering across drain rounds — Worker
        // flushes before returning from ProcessSend.
        if (send_sink_) {
            send_sink_->push_back(std::move(packet));
            LOG_DEBUG("BaseConnection::SendBuffer: queued %u bytes for batch, sock=%d",
                      buffer->GetDataLength(), send_sock);
            return true;
        }

        if (!sender_->Send(packet)) {
            LOG_ERROR("BaseConnection::SendBuffer: sender_->Send() failed");
            common::Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
            return false;
        }

        LOG_DEBUG("BaseConnection::SendBuffer: sent %u bytes via sender_, sock=%d", buffer->GetDataLength(), send_sock);
        return true;
    }

    LOG_ERROR("BaseConnection::SendBuffer: no sender available");
    common::Metrics::CounterInc(common::MetricsStd::DiagSendBufferFail);
    return false;
}

bool BaseConnection::SendImmediateAck(PacketNumberSpace ns) {
    LOG_DEBUG("BaseConnection::SendImmediateAck: ns=%d", ns);

    // 1. Determine encryption level from packet number space
    EncryptionLevel target_level;
    switch (ns) {
        case kInitialNumberSpace:
            target_level = kInitial;
            break;
        case kHandshakeNumberSpace:
            target_level = kHandshake;
            break;
        case kApplicationNumberSpace:
            target_level = kApplication;
            break;
        default:
            LOG_WARN("BaseConnection::SendImmediateAck: invalid packet number space %d", ns);
            return false;
    }

    // 2. Get cryptographer
    auto cryptographer = connection_crypto_.GetCryptographer(target_level);
    if (!cryptographer) {
        LOG_WARN("BaseConnection::SendImmediateAck: no cryptographer for level=%d", target_level);
        return false;
    }

    // 3. Generate ACK frame
    auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), ns, ecn_enabled_);
    if (!ack_frame) {
        LOG_DEBUG("BaseConnection::SendImmediateAck: no ACK to send for ns=%d", ns);
        return false;
    }

    // 4. Use PacketBuilder to build ACK packet - allocate buffer chunk first
    auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("BaseConnection::SendImmediateAck: failed to allocate buffer chunk");
        return false;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    auto result = packet_builder_->BuildAckPacket(target_level, cryptographer, ack_frame,
        cid_coordinator_->GetLocalConnectionIDManager().get(), cid_coordinator_->GetRemoteConnectionIDManager().get(),
        buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl(), connection_crypto_.GetVersion(),
        connection_crypto_.GetCurrentKeyPhase());

    if (!result.success) {
        LOG_ERROR("BaseConnection::SendImmediateAck: failed to build packet: %s", result.error_message.c_str());
        return false;
    }

    LOG_DEBUG("BaseConnection::SendImmediateAck: built ACK packet pn=%llu, size=%u", result.packet_number,
        result.packet_size);

    // 5. Send immediately
    return SendImmediate(buffer);
}

bool BaseConnection::SendImmediateFrame(std::shared_ptr<IFrame> frame, EncryptionLevel level) {
    LOG_DEBUG("BaseConnection::SendImmediateFrame: frame_type=%d, level=%d", frame->GetType(), level);

    // 1. Get cryptographer
    auto cryptographer = connection_crypto_.GetCryptographer(level);
    if (!cryptographer) {
        LOG_ERROR("BaseConnection::SendImmediateFrame: no cryptographer for level=%d", level);
        return false;
    }

    // 2. Use PacketBuilder to build single-frame packet - allocate buffer chunk first
    auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("BaseConnection::SendImmediateFrame: failed to allocate buffer chunk");
        return false;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    auto result = packet_builder_->BuildImmediatePacket(frame, level, cryptographer,
        cid_coordinator_->GetLocalConnectionIDManager().get(), cid_coordinator_->GetRemoteConnectionIDManager().get(),
        buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl(), connection_crypto_.GetVersion(),
        connection_crypto_.GetCurrentKeyPhase());

    if (!result.success) {
        LOG_ERROR(
            "BaseConnection::SendImmediateFrame: failed to build packet: %s", result.error_message.c_str());
        return false;
    }

    LOG_DEBUG(
        "BaseConnection::SendImmediateFrame: built packet pn=%llu, size=%u", result.packet_number, result.packet_size);

    // 3. Send immediately
    return SendImmediate(buffer);
}

}  // namespace quic
}  // namespace quicx