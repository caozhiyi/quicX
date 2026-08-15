#include <cstdio>
#include <cstring>

#include <quicx/common/if_event_loop.h>
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/buffer/buffer_chunk.h"
#include "common/buffer/buffer_span.h"
#include "common/buffer/single_block_buffer.h"
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/qlog/qlog.h"
#include "common/util/time.h"
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
    last_communicate_time_(0),
    ecn_enabled_(ecn_enabled),
    send_flow_controller_(start),
    recv_flow_controller_(start),
    recv_control_(loop),
    send_manager_(loop),
    packet_builder_(std::make_unique<PacketBuilder>()),
    state_machine_(this),
    event_loop_(loop),
    is_server_(start == StreamIDGenerator::StreamStarter::kServer) {
    // Sole owner of version state. Its TP-push callback routes back here
    // because EncodeAndPushTpToTls also serves the general (non-version) TP
    // path at setup, which is not the negotiator's business.
    version_negotiator_ = std::make_unique<VersionNegotiator>(is_server_, connection_crypto_, transport_param_);
    version_negotiator_->SetPushTransportParamCallback([this](TransportParam& tp) { return EncodeAndPushTpToTls(tp); });
    version_negotiator_->SetCloseConnectionCallback(
        [this](uint64_t error, uint16_t trigger_frame, std::string reason) {
            InnerConnectionClose(error, trigger_frame, std::move(reason));
        });

    // Metrics: Record handshake start time (wall clock; see field comment in
    // connection_base.h — this is intentionally NOT the monotonic clock used
    // for RTT/PTO).
    handshake_start_wall_time_ms_ = common::UTCTimeMsec();
    connection_crypto_.SetRemoteTransportParamCB([this](auto& tp) { OnTransportParams(tp); });
    // Lets the crypto layer reject the server-only transport parameters when it is
    // the server reading a client's parameters (RFC 9000 §18.2).
    connection_crypto_.SetIsServer(is_server_);

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
    connection_crypto_.SetHandshakeErrorCB(
        [this](uint64_t error, const std::string& reason) { InnerConnectionClose(error, 0, reason); });

    // Initialize connection ID coordinator (refactored)
    cid_coordinator_ = std::make_unique<ConnectionIDCoordinator>(
        loop, send_manager_, [this](auto& cid) { AddConnectionId(cid); },
        [this](auto& cid) { RetireConnectionId(cid); });
    cid_coordinator_->Initialize();

    send_manager_.SetSendRetryCallBack([this]() { OnConnectionActive(); });
    send_manager_.SetSendFlowController(&send_flow_controller_);

    // RFC 9002 §6.2.2.1: During handshake, if PTO fires and no ACK-eliciting
    // data to retransmit, send PING to elicit ACK from peer (anti-amplification)
    send_manager_.GetSendControl().SetProbeNeededCallback([this]() {
        LOG_INFO("Handshake probe: sending PING frame to elicit ACK");
        auto ping = std::make_shared<PingFrame>();
        OnFrameReady(ping);
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
        OnFrameReady(ping);
        OnConnectionActive();
    });

    // RFC 9000: Setup immediate ACK callback for Initial/Handshake/out-of-order packets
    recv_control_.SetImmediateAckCB([this](PacketNumberSpace ns) { SendImmediateAck(ns); });

    // Setup delayed ACK callback for normal Application packets
    recv_control_.SetActiveSendCB([this]() { OnConnectionActive(); });

    transport_param_.AddTransportParamListener([this](const auto& tp) { recv_control_.UpdateConfig(tp); });
    transport_param_.AddTransportParamListener([this](const auto& tp) { send_manager_.UpdateConfig(tp); });
    transport_param_.AddTransportParamListener([this](const auto& tp) { send_flow_controller_.UpdateConfig(tp); });
    transport_param_.AddTransportParamListener([this](const auto& tp) { recv_flow_controller_.UpdateConfig(tp); });

    // Set stream data ACK callback for tracking stream completion
    send_manager_.GetSendControl().SetStreamDataAckCallback(
        [this](auto a, auto b, auto c, auto d) { OnStreamDataAcked(a, b, c, d); });

    // Initialize timer coordinator (refactored)
    timer_coordinator_ = std::make_unique<TimerCoordinator>(loop, transport_param_, send_manager_, state_machine_);

    // Initialize path manager (refactored)
    // Constructor takes a single Deps struct (named-field init) instead of
    // an 8-positional-argument list — see PathManager::Deps in
    // connection_path_manager.h for the lifetime contract.
    PathManager::Deps path_deps;
    path_deps.event_loop = loop;
    path_deps.send_manager = &send_manager_;
    path_deps.cid_coordinator = cid_coordinator_.get();
    path_deps.transport_param = &transport_param_;
    path_deps.peer_addr = &peer_addr_;
    path_deps.to_send_frame_cb = [this](auto&& f) { OnFrameReady(std::forward<decltype(f)>(f)); };
    path_deps.active_send_cb = [this]() { OnConnectionActive(); };
    path_deps.set_peer_addr_cb = [this](const common::Address& addr) { this->SetPeerAddress(addr); };
    path_manager_ = std::make_unique<PathManager>(std::move(path_deps));

    // Initialize encryption level scheduler (refactored) - centralizes encryption level selection
    encryption_scheduler_ =
        std::make_unique<EncryptionLevelScheduler>(connection_crypto_, recv_control_, *path_manager_);

    // Sole owner of the egress path. The address provider warms the sockaddr
    // cache on peer_addr_ — our long-lived storage — before handing out the
    // copy that lands on the NetPacket. Without this warm-up every NetPacket
    // Address starts empty and UdpSender::SendBatch's fast-path probe degrades
    // to a per-packet sendto for the whole batch (observed udp_sb_ok=0 on a
    // 500MB upload). Both family slots are populated so v4-on-v6-dualstack and
    // v6 sockets all hit the cache; EnsureSockaddrCache is idempotent, so the
    // first call is one inet_pton and later calls are a single bool check.
    //
    // The old SendImmediate path skipped this entirely, which is why handshake
    // packets and immediate ACKs used to miss the cache.
    emitter_ = std::make_unique<DatagramEmitter>(
        send_manager_.GetSendControl(),
        [this]() {
            peer_addr_.EnsureSockaddrCache(AF_INET);
            peer_addr_.EnsureSockaddrCache(AF_INET6);
            return AcquireSendAddress();
        },
        nullptr /* qlog trace installed later via SetQlogTrace */);

    // RFC 9000 §8.1. The emitter is the only place datagrams leave the connection,
    // so hooking the budget here means no send path can bypass it — including
    // handshake packets, coalesced Initial/Handshake, retransmits and bare ACKs.
    emitter_->SetAmpBudgetCheck([this](uint32_t bytes) { return send_manager_.CheckAndChargeAmpBudget(bytes); });

    // Sole owner of socket lifecycle across migration. It performs the socket
    // switch and the close(2); we only forward the finished event to the
    // application (that needs shared_from_this(), which only we can do).
    migration_controller_ =
        std::make_unique<MigrationController>(state_machine_, *path_manager_, transport_param_, *emitter_, event_loop_);

    // The controller owns the loop-thread dispatch for migration; hand it the
    // connection's address resolvers so it can pick the migration address family
    // on the loop thread (race-free) rather than on the caller's thread.
    migration_controller_->SetLocalAddressResolver(
        [this](std::string& ip, uint32_t& port) { GetLocalAddr(ip, port); });
    migration_controller_->SetPeerAddressResolver([this]() { return GetPeerAddress(); });
    migration_controller_->SetMigrationFinishedCallback(
        [this](const MigrationInfo& info) { OnMigrationFinished(info); });
    migration_controller_->SetLocalAddressUpdatedCallback(
        [this](const common::Address& addr) { local_addr_ = addr; });

    // Initialize stream manager (refactored) - uses IConnectionEventSink interface (no callbacks!)
    stream_manager_ = std::make_unique<StreamManager>(
        *this, loop, transport_param_, send_manager_, stream_state_cb_, &send_flow_controller_);

    // Inject stream manager into send manager for stream scheduling
    send_manager_.SetStreamManager(stream_manager_.get());

    // Initialize connection closer (refactored)
    connection_closer_ =
        std::make_unique<ConnectionCloser>(loop, state_machine_, send_manager_, transport_param_, connection_close_cb_);

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
    emitter_->SetSender(std::move(sender));
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
    connection_closer_->StartGracefulClose([this]() { OnConnectionActive(); });
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
    LOG_DEBUG("BaseConnection::SetStreamStateCallBack: callback updated in both IConnection and FrameProcessor");
}

uint64_t BaseConnection::AddTimer(timer_callback callback, uint32_t timeout_ms, bool periodic) {
    if (!timer_coordinator_) {
        LOG_ERROR("BaseConnection::AddTimer: timer_coordinator_ is null");
        return 0;
    }
    return timer_coordinator_->AddTimer(callback, timeout_ms, periodic);
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
    version_negotiator_->BuildLocalVersionInformation(transport_param_);

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

uint64_t BaseConnection::GetConnectionIDHash() {
    return cid_coordinator_->GetConnectionIDHash();
}

void BaseConnection::OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    // Tie the lifetime guards of our timer controllers to this connection so
    // their timer callbacks are skipped once we are destroyed (and kept alive
    // while they run). Must happen after we are owned by a shared_ptr, which is
    // the case here; see RecvControl::Bind / SendControl::Bind.
    recv_control_.Bind(weak_from_this());
    send_manager_.Bind(weak_from_this());

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

    // qlog draft-03 §4.11: every entry in `packets` came from a single UDP
    // datagram (MsgParser::ParsePacket → DecodePackets splits coalesced
    // QUIC packets but keeps the datagram boundary intact and the caller —
    // ServerWorker / ClientWorker — invokes OnPackets once per parse
    // result). Allocate a datagram id here, stash it for the per-packet
    // QLOG_PACKET_RECEIVED sites to copy onto each PacketReceivedData, and
    // emit a single datagrams_received summary after the loop.
    const uint64_t datagram_id = next_recv_datagram_id_++;
    current_recv_datagram_id_ = datagram_id;
    std::vector<uint64_t> recv_packet_numbers;
    if (qlog_trace_) {
        recv_packet_numbers.reserve(packets.size());
    }

    // Sum of the encoded packet sizes, so the datagrams_received event matches
    // what hit the socket. Computed unconditionally (not just under qlog_trace_)
    // because the anti-amplification credit depends on it: RFC 9000 §8.1 counts
    // received datagram bytes, and gating that on qlog being enabled would make
    // a security limit depend on a debug setting.
    uint32_t recv_datagram_total_bytes = 0;
    for (const auto& packet : packets) {
        recv_datagram_total_bytes += packet->GetSrcBuffer().GetLength();
    }

    // RFC 9000 §8.1: credit the datagram against the 3x budget *before*
    // dispatching. Dispatch generates the response inline — the server answers a
    // client Initial from inside DispatchByType — so crediting afterwards meant
    // the first server flight was weighed against a still-empty budget, refused
    // by the emitter, and deferred to a later pump or PTO. Waiting for dispatch
    // buys nothing: crediting is already a no-op once the address is validated.
    send_manager_.OnCandidatePathBytesReceived(recv_datagram_total_bytes);

    bool any_packet_processed = false;
    for (size_t i = 0; i < packets.size(); i++) {
        // Packet numbers are collected here so the qlog event can list them in
        // arrival order regardless of dispatch outcome.
        bool packet_processed = DispatchByType(packets[i]);

        if (qlog_trace_) {
            recv_packet_numbers.push_back(packets[i]->GetPacketNumber());
        }

        // After processing (decrypting and decoding frames), record packet for ACK tracking
        if (packet_processed) {
            any_packet_processed = true;
            recv_control_.OnPacketRecv(now, packets[i]);
        }
    }

    // RFC 9000 §10.3.1: a Stateless Reset is deliberately indistinguishable from a
    // normal 1-RTT packet, so it can only be detected after normal processing has
    // failed. Only then is it worth checking whether the trailing 16 bytes are one
    // of the peer's reset tokens.
    if (!any_packet_processed && !packets.empty() && CheckStatelessReset(packets)) {
        return;
    }

    if (qlog_trace_) {
        common::DatagramsReceivedData dg_data;
        dg_data.datagram_id = datagram_id;
        dg_data.count = static_cast<uint32_t>(packets.size());
        dg_data.raw_length = recv_datagram_total_bytes;
        dg_data.packet_numbers = std::move(recv_packet_numbers);
        QLOG_DATAGRAMS_RECEIVED(qlog_trace_, dg_data);
    }
    // Clear the per-datagram slot once the loop is done; defensive so any
    // out-of-band log call later in the connection's lifetime doesn't pick
    // up a stale id.
    current_recv_datagram_id_ = 0;

    // reset idle timeout timer task
    timer_coordinator_->ResetIdleTimer();
}

bool BaseConnection::CheckStatelessReset(const std::vector<std::shared_ptr<IPacket>>& packets) {
    // RFC 9000 §10.3: "The Stateless Reset Token is the last 16 bytes of the
    // datagram." A reset is never coalesced, so only the final packet's buffer can
    // hold it, and the datagram must be long enough to be a plausible 1-RTT packet.
    const auto& src = packets.back()->GetSrcBuffer();
    const uint32_t length = src.GetLength();
    if (length < kMinStatelessResetSize) {
        return false;
    }

    const uint8_t* token = src.GetStart() + (length - TransportParam::kStatelessResetTokenLength);
    if (!cid_coordinator_->IsPeerStatelessResetToken(token)) {
        return false;
    }

    LOG_INFO("stateless reset received; peer has lost our connection state.");

    // §10.3: the receiver enters the draining state immediately and MUST NOT send
    // anything further on this connection -- in particular it must not reply with
    // CONNECTION_CLOSE, which would be answered with another reset. This is the
    // same transition a received CONNECTION_CLOSE drives, which is exactly the
    // "peer ended it, stay silent" semantics required here.
    state_machine_.OnConnectionCloseFrameReceived();
    if (connection_close_cb_) {
        connection_close_cb_(shared_from_this(), static_cast<uint64_t>(QuicErrorCode::kNoError), "stateless reset");
    }
    return true;
}

void BaseConnection::HandlePacketsInClosingState(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    bool has_connection_close = false;
    for (auto& packet : packets) {
        std::shared_ptr<ICryptographer> cryptographer = connection_crypto_.GetCryptographer(packet->GetCryptoLevel());
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
            EnqueueFrameDuringTermination(frame);
            connection_closer_->MarkConnectionCloseRetransmitted(current_time);        }
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
            return version_negotiator_->OnVersionNegotiationPacket(packet);
        case PacketType::kInitialPacketType:
            return OnInitialPacket(packet);
        case PacketType::k0RttPacketType:
            return On0rttPacket(packet);
        case PacketType::kHandshakePacketType: {
            if (!OnHandshakePacket(packet)) {
                return false;
            }
            // RFC 9369 §4.1 bounds how long the pre-upgrade Initial keys that
            // RekeyInitialForVersion retained may live: only until a packet in
            // the negotiated version is successfully processed. A Handshake
            // packet is a conservative choice of that moment — Handshake keys
            // are derived from the TLS handshake and therefore only ever exist
            // for the negotiated version, so getting here proves the peer has
            // seen and adopted the upgrade and will never send under the old
            // version again. Placed in the dispatcher so the base and
            // ClientConnection overrides of OnHandshakePacket share it.
            connection_crypto_.DiscardPreviousInitialKeys();

            // RFC 9000 §8.1: "a server MUST NOT send more than three times as many
            // bytes as the number of bytes it has received [...] until it has
            // validated the client's address." Successfully processing a Handshake
            // packet proves the client received our Initial (Handshake keys come
            // from the TLS handshake), so the address is validated and the
            // amplification limit is lifted.
            send_manager_.MarkAddressValidated();
            return true;
        }
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

    // RFC 9000 §7.3: remember the peer's SCID from its first Initial so that
    // ValidatePeerConnectionIds() can later confirm the peer's
    // initial_source_connection_id transport parameter agrees with it.
    RecordPeerInitialScid(header->GetSourceConnectionId(), header->GetSourceConnectionIdLength());

    // RFC 9368: Record the version the peer used in its FIRST Initial. This
    // is needed on the server side for the mandatory "peer_chosen_version
    // matches on-wire version" consistency check in
    // ValidateAndMaybeUpgradeByRemoteTP().
    version_negotiator_->RecordPeerOriginalVersion(pkt_version);

    if (!connection_crypto_.InitIsReady()) {
        // First Initial on this connection (server side first packet, or
        // client side prior to having installed keys). The DCID in the
        // packet is the one we use to derive the Initial secret.
        if (version_negotiator_->DiffersFromCurrent(pkt_version)) {
            LOG_INFO("Updating connection version from packet: 0x%08x -> 0x%08x", version_negotiator_->GetVersion(),
                pkt_version);
            // ApplyVersion also re-pushes version_information to TLS, which
            // RFC 9368 §4 requires before EncryptedExtensions is serialized.
            version_negotiator_->ApplyVersion(pkt_version);
        }

        // RFC 9000 §7.3: "The server MUST include the original_destination_connection_id
        // transport parameter", set to the DCID of the client's first Initial. That value is
        // what lets the client prove the handshake belongs to the CID it originally chose.
        // The accepting worker usually supplies it via QuicTransportParams, but back-fill it
        // here whenever it is missing so the parameter can never be dropped by an embedder
        // that builds the server connection itself. Must happen before the CRYPTO frames in
        // this very packet reach TLS, i.e. before OnNormalPacket() below.
        if (is_server_ && transport_param_.GetOriginalDestinationConnectionId().empty() &&
            header->GetDestinationConnectionIdLength() > 0) {
            transport_param_.SetOriginalDestinationConnectionId(
                std::string(reinterpret_cast<const char*>(header->GetDestinationConnectionId()),
                    header->GetDestinationConnectionIdLength()));
            EncodeAndPushTpToTls(transport_param_);
        }

        LOG_INFO("Installing Initial Secret for decryption from packet DCID: length=%u, version=0x%08x",
            header->GetDestinationConnectionIdLength(), version_negotiator_->GetVersion());
        connection_crypto_.InstallInitSecret(
            (uint8_t*)header->GetDestinationConnectionId(), header->GetDestinationConnectionIdLength(), true);

    } else if (version_negotiator_->IsPeerVersionSwitch(pkt_version)) {
        // RFC 9368 Compatible Version Negotiation (client side): the server
        // upgraded the connection. We already hold an Initial cryptographer
        // under the old version's salt, so we cannot decrypt this Initial until
        // we re-derive keys. Same mechanism the server uses when acting on our
        // version_information TP, hence the shared entry point.
        LOG_INFO("RFC 9368: client detected server version upgrade: 0x%08x -> 0x%08x",
            version_negotiator_->GetVersion(), pkt_version);

        switch (version_negotiator_->ApplyCompatibleUpgrade(pkt_version)) {
            case VersionNegotiator::UpgradeResult::kUpgraded:
                break;

            case VersionNegotiator::UpgradeResult::kNoDcid:
                // Unlike the server, the client cannot proceed without rekeying:
                // it has no key that decrypts this packet.
                LOG_ERROR("RFC 9368: cannot rekey client Initial (no cached DCID); dropping packet");
                return false;

            case VersionNegotiator::UpgradeResult::kRekeyFailed:
            default:
                LOG_ERROR("RFC 9368: client-side RekeyInitialForVersion failed");
                return false;
        }
    }

    // Resolve Initial keys by the version on the wire, not by the connection's
    // current version.
    //
    // After a Compatible VN upgrade both generations are live: the negotiated
    // one, plus the pre-upgrade one that RekeyInitialForVersion retained
    // because the peer keeps retransmitting its first flight under the original
    // version until our upgrade reaches it (RFC 9369 §4.1). Handing such a
    // packet to the current keys yields EVP_AEAD_CTX_open failed and, when the
    // peer's first flight spans several Initial packets, a stalled handshake.
    auto initial_cryptographer = connection_crypto_.GetInitialCryptographerForVersion(pkt_version);
    if (!initial_cryptographer) {
        // RFC 9369 §4.1: "An endpoint MUST drop packets using any other
        // version." We hold no Initial key for this one, so there is nothing
        // worth attempting.
        if (qlog_trace_) {
            common::PacketDroppedData drop_data;
            drop_data.packet_type = packet->GetHeader()->GetPacketType();
            drop_data.packet_size = packet->GetSrcBuffer().GetLength();
            drop_data.trigger = "unsupported_version";
            QLOG_PACKET_DROPPED(qlog_trace_, drop_data);
        }
        return false;
    }

    return OnNormalPacket(packet, initial_cryptographer);
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
            // draft-03: link this packet to its enclosing UDP datagram (set
            // by OnPackets before the dispatch loop). 0 means "no datagram
            // open" and the field is omitted from the JSON.
            data.datagram_id = current_recv_datagram_id_;
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

bool BaseConnection::OnNormalPacket(
    const std::shared_ptr<IPacket>& packet, const std::shared_ptr<ICryptographer>& cryptographer_override) {
    std::shared_ptr<ICryptographer> cryptographer =
        cryptographer_override ? cryptographer_override : connection_crypto_.GetCryptographer(packet->GetCryptoLevel());
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
        // draft-03: stamp the enclosing UDP datagram id (allocated by
        // OnPackets before the dispatch loop) so viewers can collapse
        // coalesced QUIC packets back into the originating datagram.
        data.datagram_id = current_recv_datagram_id_;

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

bool BaseConnection::ParsePreferredAddress(const std::string& value, common::Address& out) {
    if (value.empty()) {
        return false;
    }

    std::string host;
    std::string port_str;

    if (value[0] == '[') {
        // Bracketed IPv6: "[<ipv6>]:<port>".
        const auto close = value.find(']');
        if (close == std::string::npos || close == 1) {
            return false;  // no closing bracket, or empty host
        }
        if (close + 1 >= value.size() || value[close + 1] != ':') {
            return false;  // must be followed by ":<port>"
        }
        host = value.substr(1, close - 1);
        port_str = value.substr(close + 2);

    } else {
        const auto colon = value.find(':');
        if (colon == std::string::npos || colon == 0) {
            return false;  // no port, or empty host
        }
        // More than one colon without brackets is an unbracketed IPv6 literal:
        // genuinely ambiguous, so refuse instead of guessing.
        if (value.find(':', colon + 1) != std::string::npos) {
            return false;
        }
        host = value.substr(0, colon);
        port_str = value.substr(colon + 1);
    }

    if (host.empty() || port_str.empty()) {
        return false;
    }

    // Hand-rolled rather than std::stoi/strtoul: no exceptions, no errno, and
    // trailing garbage ("443x") is rejected instead of silently ignored.
    uint32_t port = 0;
    for (char c : port_str) {
        if (c < '0' || c > '9') {
            return false;
        }
        port = port * 10 + static_cast<uint32_t>(c - '0');
        if (port > 65535) {
            return false;  // bail on overflow; also catches absurdly long input
        }
    }
    if (port == 0) {
        return false;  // port 0 is not a usable destination
    }

    out = common::Address(host, static_cast<uint16_t>(port));
    return true;
}

void BaseConnection::RecordPeerInitialScid(const uint8_t* id, uint8_t len) {
    if (peer_initial_scid_recorded_) {
        return;
    }
    peer_initial_scid_recorded_ = true;
    if (id != nullptr && len > 0) {
        peer_initial_scid_.assign(reinterpret_cast<const char*>(id), len);
    }
}

bool BaseConnection::ValidatePeerConnectionIds(const TransportParam& remote_tp) {
    // RFC 9000 §7.3: "An endpoint MUST treat the following as a connection error of
    // type TRANSPORT_PARAMETER_ERROR: [...] a mismatch between values received from a
    // peer in these transport parameters and the value sent in the corresponding
    // Destination or Source Connection ID fields of Initial packets."
    if (!peer_initial_scid_recorded_) {
        // No long-header packet observed (e.g. a resumed/0-RTT-only path in tests):
        // nothing to compare against, so do not manufacture a failure.
        return true;
    }

    const std::string& tp_iscid = remote_tp.GetInitialSourceConnectionId();
    if (tp_iscid != peer_initial_scid_) {
        LOG_ERROR("initial_source_connection_id mismatch: tp_len=%zu, observed_len=%zu", tp_iscid.length(),
            peer_initial_scid_.length());
        InnerConnectionClose(QuicErrorCode::kTransportParameterError, 0, "initial_source_connection_id mismatch");
        return false;
    }
    return true;
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

    // RFC 9000 §7.3. Must also run BEFORE Merge(), which folds the peer's CID
    // parameters into our own TransportParam and would erase the evidence.
    if (!ValidatePeerConnectionIds(remote_tp)) {
        return;
    }

    // RFC 9000 §10.3.1: the stateless_reset_token transport parameter belongs to
    // the peer's initial CID. Record it so a Stateless Reset for that CID is
    // recognised rather than discarded as an undecryptable packet.
    const std::string& reset_token = remote_tp.GetStatelessResetToken();
    if (reset_token.size() == TransportParam::kStatelessResetTokenLength) {
        cid_coordinator_->AddPeerStatelessResetToken(reinterpret_cast<const uint8_t*>(reset_token.data()));
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

            common::Address addr;
            if (!ParsePreferredAddress(pref, addr)) {
                // Peer-controlled value; a bad one isignored, not fatal.
                LOG_WARN("Invalid preferred address: %s (expected host:port or [ipv6]:port)", pref.c_str());
            } else if (addr == GetPeerAddress()) {
                LOG_DEBUG("Preferred address is same as current address, no migration needed");
            } else {
                LOG_INFO("Client initiating migration to server's preferred address: %s:%d", addr.GetIp().c_str(),
                    addr.GetPort());
                path_manager_->OnObservedPeerAddress(addr);
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
        LOG_WARN("Connection idle timeout: %u consecutive PTOs without ACK, closing connection", consecutive_ptos);

        // Metrics: PTO count
        common::Metrics::CounterInc(common::MetricsStd::PtoCountTotal);

        // Close with no error (idle timeout is normal termination)
        InnerConnectionClose(QuicErrorCode::kNoError, 0, "Persistent PTO timeout");
    }
}

void BaseConnection::OnFrameReady(std::shared_ptr<IFrame> frame) {
    // Sole "enqueue a frame" entry point. Queueing and waking are inseparable
    // here on purpose: the old split (SendManager::ToSendFrame for queue-only
    // vs BaseConnection::ToSendFrame for queue+wake — same name, different
    // semantics) forced every caller to know which one it wanted, and two call
    // sites had to hand-patch the missing wake-up afterwards.
    send_manager_.EnqueueFrame(frame);
    OnConnectionActive();
}

void BaseConnection::OnStreamDataReady(std::shared_ptr<IStream> stream) {
    if (state_machine_.IsTerminating()) {
        return;
    }
    // Guard against accessing stream_manager_ after destruction
    if (!stream_manager_) {
        return;
    }
    if (stream->GetStreamID() != 0) {
        // Notify scheduler that early data (0-RTT) might be needed
        encryption_scheduler_->SetEarlyDataPending(true);
    }
    stream_manager_->MarkStreamActive(stream);
    OnConnectionActive();
}

EncryptionLevel BaseConnection::GetCurEncryptionLevel() {
    // Thin delegate. The 0-RTT send-ordering decision (Initial before
    // EarlyData) belongs to EncryptionLevelScheduler, which the send path
    // drives via SetEarlyDataPending() / SetInitialPacketSent(). The branch
    // that used to live here was unreachable anyway: its initial_packet_sent_
    // flag had no writer in the entire tree.
    return connection_crypto_.GetCurEncryptionLevel();
}

void BaseConnection::OnObservedPeerAddress(const common::Address& addr) {
    if (path_manager_) {
        path_manager_->OnObservedPeerAddress(addr);
    }
}

void BaseConnection::EnqueueFrameDuringTermination(std::shared_ptr<IFrame> frame) {
    // OnConnectionActive() deliberately suppresses wake-ups once
    // IsTerminating() is true, to stop retransmission churn on a dying
    // connection. CONNECTION_CLOSE has to defeat that suppression or it would
    // never leave the host, so this path pokes active_connection_cb_ directly.
    //
    // This exists as a named operation on purpose: the bypass used to be
    // open-coded at both call sites, where it read like a redundant hand-patch
    // rather than a deliberate exception.
    send_manager_.EnqueueFrame(frame);
    if (active_connection_cb_) {
        LOG_DEBUG("EnqueueFrameDuringTermination: forcing send while terminating");
        active_connection_cb_(shared_from_this());
    }
}

// ==================== IConnectionEventSink Implementation ====================
// These methods replace callback-based event notification with direct method calls,
// reducing std::bind overhead and improving performance.

void BaseConnection::OnConnectionActive() {
    common::Metrics::CounterInc(common::MetricsStd::DiagActiveSendCalls);
    // Don't trigger send retry if connection is closing, draining, or closed
    // This prevents unnecessary retransmissions when connection is terminating
    if (state_machine_.IsTerminating()) {
        LOG_DEBUG("OnConnectionActive called but connection is terminating, ignoring, state=%d",
            static_cast<int>(state_machine_.GetState()));
        return;
    }

    // NOTE: this is on the per-send hot path (one call per outbound packet, one
    // per ACK delivery, one per stream wakeup, one per timer fire). At INFO
    // level under a 25k-request load it produces ~30k log lines/second per
    // worker, all funneled through the synchronous file logger -- the worker
    // thread blocks on disk IO and visibly "freezes" for 5-10 seconds in the
    // middle of a benchmark. Keep at DEBUG.
    if (active_connection_cb_) {
        LOG_DEBUG("OnConnectionActive: invoking active_connection_cb_");
        active_connection_cb_(shared_from_this());
    } else {
        LOG_WARN("OnConnectionActive: active_connection_cb_ is null!");
    }
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
    connection_closer_->StartImmediateClose(error, trigger_frame, reason, [this]() { OnConnectionActive(); });
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
    // All migration state (paths, connection IDs, sockets, timers) is owned by
    // this connection's event-loop thread. MigrationController performs the
    // thread hop internally and waits for the result, so this call is safe from
    // any thread. Address-family resolution is likewise done inside the
    // controller on the loop thread, so it never races the cached local/peer
    // addresses that live there. The weak owner lets the controller skip the
    // deferred task if the connection is destroyed while the request is queued.
    return migration_controller_->InitiateMigration(
        std::weak_ptr<void>(std::static_pointer_cast<BaseConnection>(shared_from_this())));
}

MigrationResult BaseConnection::InitiateMigrationTo(const std::string& local_ip, uint16_t local_port) {
    return migration_controller_->InitiateMigrationTo(
        std::weak_ptr<void>(std::static_pointer_cast<BaseConnection>(shared_from_this())), local_ip, local_port);
}

void BaseConnection::SetMigrationCallback(migration_callback cb) {
    migration_cb_ = cb;
}

void BaseConnection::GetLocalAddr(std::string& addr, uint32_t& port) {
    // Cached value wins.
    if (!local_addr_.GetIp().empty()) {
        addr = local_addr_.GetIp();
        port = local_addr_.GetPort();
        return;
    }

    // Otherwise query the socket. The active fd (primary, or the probe socket
    // while a migration is in flight) is owned by the emitter.
    const int32_t sock = emitter_->GetActiveSocket();
    if (sock > 0) {
        common::Address local;
        if (GetLocalAddressFromSocket(sock, local)) {
            local_addr_ = local;
            addr = local_addr_.GetIp();
            port = local_addr_.GetPort();
            return;
        }
    }

    addr = "";
    port = 0;
}

bool BaseConnection::IsMigrationSupported() const {
    return migration_controller_->IsMigrationSupported();
}

bool BaseConnection::IsMigrationInProgress() const {
    return migration_controller_->IsMigrationInProgress();
}

void BaseConnection::OnMigrationFinished(const MigrationInfo& info) {
    // Socket switching / retiring already happened inside MigrationController;
    // all that is left is telling the application. This lives here because
    // building the IQuicConnection shared_ptr needs shared_from_this().
    if (migration_cb_) {
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
    // ClearActiveStreams() already drops the pending frame list.
    send_manager_.ClearActiveStreams();

    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(connection_closer_->GetClosingErrorCode());
    frame->SetErrFrameType(connection_closer_->GetClosingTriggerFrame());
    frame->SetReason(connection_closer_->GetClosingReason());

    // Queue CONNECTION_CLOSE and force it onto the wire: the normal wake-up
    // path is suppressed in Closing state, so this uses the explicit
    // terminating-state entry point.
    EnqueueFrameDuringTermination(frame);

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
    // Fire-and-forget: nothing ever cancels this, and the weak_ptr in the closure
    // is its own lifetime guard. PostDelayed says exactly that, and hands back no
    // handle that could be dropped by accident.
    loop->PostDelayed(
        [weak_self]() {
            auto self = weak_self.lock();
            if (!self) return;
            self->OnClosingTimeout();
        },
        wait_ms);
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
    // ClearActiveStreams() already drops the pending frame list.
    send_manager_.ClearActiveStreams();

    // IMPORTANT: Same self-pinning as OnStateToClosing — grab shared_from_this()
    // BEFORE the callback to keep `this` alive through the timer setup.
    // Timer callback uses weak_ptr to avoid EventLoop→Connection cycle.
    auto self = shared_from_this();
    connection_closer_->InvokeConnectionCloseCallback(
        self, connection_closer_->GetClosingErrorCode(), connection_closer_->GetClosingReason());

    auto loop = event_loop_.lock();
    if (!loop) return;
    auto weak_self = weak_from_this();
    loop->PostDelayed(
        [weak_self]() {
            auto self = weak_self.lock();
            if (!self) return;
            self->OnClosingTimeout();
        },
        connection_closer_->GetCloseWaitTime() * 3);
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
        LOG_WARN(
            "BaseConnection::TrySendRetransmit: no cryptographer for lost packet level=%d, dropping", crypto_level);
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

    // Payload snapshot for retransmit debugging, paired with the "first-send"
    // dump in PacketBuilder::BuildDataPacket. Both are gated: snprintf×16 plus
    // LOG_INFO per packet was measured at ~5us/packet there, and this side runs
    // once per retransmission -- i.e. hottest exactly during the loss storms it
    // exists to diagnose. The gate was previously only applied to the other
    // half of the pair.
#ifdef QUICX_DIAG_RTX
    if (auto rtt1 = std::dynamic_pointer_cast<Rtt1Packet>(lost_pkt)) {
        auto pl = rtt1->GetPayload();
        char head[64] = {0};
        uint32_t dump_len = pl.GetLength() < 16 ? pl.GetLength() : 16;
        for (uint32_t i = 0; i < dump_len; ++i) {
            std::snprintf(head + i * 3, sizeof(head) - i * 3, "%02x ", pl.Valid() ? pl.GetStart()[i] : 0);
        }
        LOG_INFO(
            "[DIAG-RTX] retransmit-pre orig_pn=%llu new_pn=%llu payload_len=%u "
            "payload_valid=%d chunk=%p head=%s",
            (unsigned long long)orig_pn, (unsigned long long)new_pn, pl.GetLength(), (int)pl.Valid(),
            (void*)pl.GetChunk().get(), head);
    }
#else
    (void)orig_pn;
#endif

    if (!lost_pkt->Encode(buffer)) {
        LOG_ERROR("BaseConnection::TrySendRetransmit: failed to re-encode lost packet pn=%llu", new_pn);
        return false;
    }

    uint32_t encoded_size = buffer->GetDataLength();

    // qlog draft-03: open a fresh datagram before OnPacketSend so the
    // retransmitted packet's packet_sent event carries its datagram_id.
    auto scope = emitter_->Open();

    // Record this retransmission in SendControl carrying the original
    // stream_data, otherwise an ACK on the new PN would not flow back to
    // SendStream::OnDataAcked and the byte-range bookkeeping would be
    // permanently missing the bytes that the retransmit just delivered.
    send_control.OnPacketSend(common::UTCTimeMsec(), lost_pkt, encoded_size, lost_entry.stream_data);

    // The stream_data byte ranges this retransmission carries. Building the
    // summary string is unconditional work, so it stays behind the diagnostic
    // gate; without it, a loss storm pays a heap allocation and several
    // std::to_string calls per retransmitted packet, on top of a synchronous
    // INFO-level log line. (The same lesson is recorded on OnConnectionActive,
    // where INFO-level logging on a per-send path stalled the worker thread on
    // disk IO for seconds at a time.)
#ifdef QUICX_DIAG_RTX
    std::string sd_summary;
    for (const auto& sd : lost_entry.stream_data) {
        sd_summary += "{sid=" + std::to_string(sd.stream_id) + ",off=" + std::to_string(sd.offset_start) +
                      ",len=" + std::to_string(sd.length) + ",fin=" + std::to_string(sd.has_fin) + "}";
    }
    LOG_INFO(
        "BaseConnection::TrySendRetransmit: retransmitted lost packet with new pn=%llu, size=%u, "
        "stream_data count=%zu ranges=%s",
        new_pn, encoded_size, lost_entry.stream_data.size(), sd_summary.c_str());
#else
    LOG_DEBUG("BaseConnection::TrySendRetransmit: retransmitted pn=%llu, size=%u, stream_data count=%zu", new_pn,
        encoded_size, lost_entry.stream_data.size());
#endif

    return scope.Commit(buffer);
}

int BaseConnection::TrySendBurst(int budget) {
    common::Metrics::CounterInc(common::MetricsStd::DiagTrySendIters);
    // Tie the lifetime guards of our timer controllers to this connection (see
    // OnPackets for rationale). Ensures PTO / retransmit callbacks are skipped
    // once we are destroyed.
    recv_control_.Bind(weak_from_this());
    send_manager_.Bind(weak_from_this());
    if (budget <= 0) {
        return 0;
    }
    // 1. State check - allow Connecting, Connected, and Closing states
    if (state_machine_.IsClosed() || state_machine_.IsDraining()) {
        LOG_DEBUG("BaseConnection::TrySendBurst: connection is closed/draining, state=%d", state_machine_.GetState());
        return 0;
    }
    int sent = 0;
    // Retransmit path keeps its own per-call cadence (loss queue churn,
    // separate cryptographer per lost packet). We loop one-packet at a
    // time here so RFC 9001 §6.5 key-phase handling stays unchanged.
    while (sent < budget && send_manager_.GetSendControl().NeedReSend()) {
        if (!TrySendRetransmit()) {
            // No more retransmits to issue right now; fall through to the
            // fresh-data burst below.
            break;
        }
        ++sent;
    }
    if (sent < budget) {
        // RFC 9000 §12.2 Initial+Handshake coalescing.
        //
        // Before falling into the regular level-sticky burst path, if the
        // round simultaneously has Initial *and* Handshake CRYPTO bytes
        // queued, try to emit one coalesced UDP datagram carrying both
        // QUIC packets. This is the cheapest way to drain the handshake:
        //   - 1 UDP datagram instead of 2 (halves syscall + cwnd-tax overhead)
        //   - 1 round-trip elimination opportunity for the peer (it can
        //     ACK both PN spaces from a single delivery)
        //   - Padding (RFC §14.1 client first-flight) lives inside the
        //     Handshake packet, keeping the Initial small.
        //
        // The detection is intentionally limited to the handshake state
        // (Initial keys not yet discarded, both cryptographers installed)
        // so it never fires once 1-RTT is established. A return of 0 from
        // TryCoalescedInitialHandshake means "not applicable or build
        // failed"; in that case we just fall through to the legacy
        // single-level burst below — never a hard failure of the round.
        if (state_machine_.GetState() == ConnectionStateType::kStateConnecting) {
            int coalesced = TryCoalescedInitialHandshake();
            if (coalesced > 0) {
                // Clamp: a coalesced datagram reports 2 packets, so an
                // unclamped += would let TrySendBurst(1) return 2 and break
                // the documented "returns <= budget" contract.
                const int room = budget - sent;
                sent += (coalesced > room) ? room : coalesced;
            }
        }
    }
    if (sent < budget) {
        sent += TrySendNewBurst(budget - sent);
    }
    if (sent > 0) {
        common::Metrics::HistogramObserve(common::MetricsStd::DiagTrySendBurstPkts, static_cast<uint64_t>(sent));
    }
    return sent;
}

void BaseConnection::FillPacketIdentity(
    PacketBuilder::DataPacketContext& ctx, EncryptionLevel level, std::shared_ptr<ICryptographer> cryptographer) {
    ctx.level = level;
    ctx.cryptographer = std::move(cryptographer);
    ctx.local_cid_manager = cid_coordinator_->GetLocalConnectionIDManager().get();
    ctx.remote_cid_manager = cid_coordinator_->GetRemoteConnectionIDManager().get();
    ctx.quic_version = connection_crypto_.GetVersion();
    ctx.stream_manager = stream_manager_.get();
}

void BaseConnection::MaybePrependAck(std::vector<std::shared_ptr<IFrame>>& frames, PacketNumberSpace ns) {
    if (!recv_control_.ShouldSendAckNow(ns)) {
        return;
    }
    auto ack = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), ns, ecn_enabled_);
    if (ack) {
        frames.insert(frames.begin(), ack);
    }
}

int BaseConnection::TrySendNewBurst(int budget) {
    // Burst-mode core of TrySendNew. We hoist *all* per-call constants
    // (encryption level, cryptographer, key phase, version, CID managers,
    // padding/min_size policy, token) out of the inner loop so the hot
    // path per packet only runs the work that genuinely changes:
    //   * cwnd headroom (mutates after every Send → SentPacketManager
    //     credits / debits bytes_in_flight)
    //   * connection-level flow control slack
    //   * pending frame list
    //   * chunk allocation, packet build, SendBuffer, post-send accounting
    if (budget <= 0) {
        return 0;
    }

    // 2. Get send context (determine encryption level) ONCE per burst.
    // This is the cheapest hoist: a couple of bool checks + a struct copy.
    // The only mutation it can drive is SetInitialPacketSent(true) at the
    // tail of each successful build — which only matters for kInitial and
    // is preserved inside the loop below.
    auto send_ctx = encryption_scheduler_->GetNextSendContext();
    LOG_DEBUG("BaseConnection::TrySendBurst: selected encryption level=%d", send_ctx.level);

    // 3. Cryptographer (one shared_ptr fetch per burst instead of one per packet).
    auto cryptographer = connection_crypto_.GetCryptographer(send_ctx.level);
    if (!cryptographer) {
        LOG_ERROR("BaseConnection::TrySendBurst: no cryptographer for level=%d", send_ctx.level);
        return 0;
    }

    // 4. Packet builder fixed fields. Everything below survives a full burst.
    PacketBuilder::DataPacketContext tmpl;
    FillPacketIdentity(tmpl, send_ctx.level, cryptographer);
    tmpl.key_phase = connection_crypto_.GetCurrentKeyPhase();
    tmpl.add_padding = (send_ctx.level == kInitial);
    tmpl.min_size = kMinInitialPacketSize;  // RFC 9000 §14.1
    tmpl.token = send_manager_.GetToken();

    int sent = 0;
    bool ack_attached_this_burst = false;
    while (sent < budget) {
        // 4a. cwnd recheck (mutates after every successful Send: the
        // sent-packet manager credits/debits bytes_in_flight, so an
        // earlier packet in this burst can push cwnd to 0).
        uint32_t max_bytes = send_manager_.GetAvailableWindow();
        if (max_bytes == 0) {
            // RFC 9000 §9.3.3 probing-frame exemption.
            bool has_probing = send_manager_.HasPendingProbingFrame();
            if (has_probing) {
                max_bytes = kMinInitialPacketSize;
                LOG_DEBUG(
                    "BaseConnection::TrySendBurst: cwnd full but probing frame pending — bypassing cwnd (RFC 9000 "
                    "§9.3.3)");
            } else {
                LOG_DEBUG("BaseConnection::TrySendBurst: congestion window full at pkt #%d", sent);
                send_manager_.SetCwndLimited();
                common::Metrics::CounterInc(common::MetricsStd::DiagTrySendCwndBlocked);
                break;
            }
        }

        // 4b. Pending frames (always per-packet — GetPendingFrames may
        // hand out at most one packet's worth of frames at a time).
        auto frames = send_manager_.GetPendingFrames(send_ctx.level, max_bytes);

        // 4c. ACK piggyback. ShouldSendAckNow is consumed by the ACK
        // emission path (RecvControl::MayGenerateAckFrame clears the
        // pending flag), so only the FIRST iteration of a burst attaches
        // the queued ACK — subsequent packets get fresh ACKs only if a
        // new one becomes due, exactly as the original one-packet-per-
        // TrySend path behaved.
        if (send_ctx.has_pending_ack && !ack_attached_this_burst) {
            auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), send_ctx.ack_space, ecn_enabled_);
            if (ack_frame) {
                frames.insert(frames.begin(), ack_frame);
                LOG_DEBUG("BaseConnection::TrySendBurst: added ACK frame for ns=%d", send_ctx.ack_space);
            }
            ack_attached_this_burst = true;
        }

        bool has_stream_data = send_manager_.HasStreamData(send_ctx.level);
        if (frames.empty() && !has_stream_data) {
            LOG_DEBUG("BaseConnection::TrySendBurst: no data to send at pkt #%d", sent);
            common::Metrics::CounterInc(common::MetricsStd::DiagTrySendNoData);
            break;
        }

        // 4d. Per-packet build context (copies of the hoisted template +
        // freshly-computed bits). The copy is a handful of pointers — far
        // cheaper than the work we just elided.
        PacketBuilder::DataPacketContext build_ctx = tmpl;
        build_ctx.frames = std::move(frames);
        build_ctx.include_stream_data = has_stream_data;

        // 4e. Connection-level flow control.
        //
        // A block here does not abort the packet: it only zeroes the STREAM-byte
        // allowance. CRYPTO frames are exempt (RFC 9000 §4.1) and honour no such
        // limit — FixBufferFrameVisitor::HandleFrame applies the allowance only
        // on the is_stream branch, and CryptoStream::TrySendData never consults
        // it — so a handshake cannot stall behind a full connection FC window.
        uint64_t conn_flow_limit = 0;
        std::shared_ptr<IFrame> blocked_frame;
        bool fc_blocked_with_data = false;
        if (send_flow_controller_.CanSendData(conn_flow_limit, blocked_frame)) {
            build_ctx.max_stream_data_size = static_cast<uint32_t>(std::min<uint64_t>(conn_flow_limit, UINT32_MAX));
            if (has_stream_data && build_ctx.max_stream_data_size < 32) {
                // DEBUG, not INFO: this fires on every burst iteration while the
                // connection FC window is nearly exhausted, which is a sustained
                // condition rather than a one-off event.
                LOG_DEBUG(
                    "BaseConnection::TrySendBurst budget: max_bytes(cwnd)=%u, conn_flow_limit=%llu, "
                    "max_stream_data_size=%u (<32) — stream frame header may not fit",
                    max_bytes, (unsigned long long)conn_flow_limit, build_ctx.max_stream_data_size);
            }
            if (blocked_frame) {
                LOG_DEBUG("BaseConnection::TrySendBurst: queueing proactive DATA_BLOCKED frame (near limit)");
                send_manager_.EnqueueFrame(blocked_frame);
            }
        } else {
            build_ctx.max_stream_data_size = 0;
            LOG_DEBUG("BaseConnection::TrySendBurst: connection-level FC blocked, blocked_frame=%p, has_stream_data=%d",
                blocked_frame.get(), has_stream_data ? 1 : 0);
            if (blocked_frame) {
                send_manager_.EnqueueFrame(blocked_frame);
            }
            if (has_stream_data) {
                fc_blocked_with_data = true;
            }
        }
        if (fc_blocked_with_data) {
            send_manager_.SetFlowControlBlocked();
        }

        // 4f. Buffer chunk + packet build.
        auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
        if (!chunk || !chunk->Valid()) {
            LOG_ERROR("BaseConnection::TrySendBurst: failed to allocate buffer chunk at pkt #%d", sent);
            break;
        }
        auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

        // qlog draft-03: every iteration of this burst loop ships exactly
        // one QUIC packet inside its own UDP datagram (coalescing only
        // happens via the dedicated TryCoalescedInitialHandshake() path),
        // so a fresh datagram_id per iteration is the correct binding.
        // Scope is loop-body scoped: it drains on `break` as well as on the
        // normal path, so no iteration can leak an open bracket.
        auto scope = emitter_->Open();

        uint64_t t_build_start = common::Metrics::NowUs();
        auto result = packet_builder_->BuildDataPacket(
            build_ctx, buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl());
        {
            uint64_t t_build_end = common::Metrics::NowUs();
            if (t_build_end > t_build_start) {
                common::Metrics::HistogramObserve(common::MetricsStd::DiagBuildLatencyUs, t_build_end - t_build_start);
            }
        }

        if (!result.success) {
            LOG_ERROR(
                "BaseConnection::TrySendBurst: failed to build packet: %s "
                "[max_bytes(cwnd)=%u, conn_flow_limit=%llu, max_stream_data_size=%u, "
                "frames=%zu, has_stream_data=%d]",
                result.error_message.c_str(), max_bytes, (unsigned long long)conn_flow_limit,
                build_ctx.max_stream_data_size, build_ctx.frames.size(), has_stream_data ? 1 : 0);
            if (has_stream_data && !fc_blocked_with_data) {
                send_manager_.SetFlowControlBlocked();
            }
            common::Metrics::CounterInc(common::MetricsStd::DiagTrySendBuildFail);
            // qlog draft-03: Scope's destructor discards the bracket opened
            // above so a subsequent packet doesn't inherit a stale (empty) id.
            break;
        }

        LOG_DEBUG("BaseConnection::TrySendBurst: built packet pn=%llu, size=%u bytes", result.packet_number,
            result.packet_size);

        // 4g. Initial-packet bookkeeping. Same semantics as the original
        // one-packet-per-call path: flip the scheduler bit exactly once
        // (the helper is idempotent).
        if (send_ctx.level == kInitial) {
            encryption_scheduler_->SetInitialPacketSent(true);
        }

        // 4h. Send (or enqueue into the worker batch sink).
        uint64_t t_send_start = common::Metrics::NowUs();
        bool send_success = scope.Commit(buffer);
        {
            uint64_t t_send_end = common::Metrics::NowUs();
            if (t_send_end > t_send_start) {
                common::Metrics::HistogramObserve(common::MetricsStd::DiagSendLatencyUs, t_send_end - t_send_start);
            }
        }

        if (send_success) {
            common::Metrics::HistogramObserve(common::MetricsStd::DiagPktPayloadHist, buffer->GetDataLength());
        }

        // 4i. Conn-level FC accounting + key update trigger (unchanged).
        if (send_success && result.stream_data_size > 0) {
            send_flow_controller_.OnDataSent(result.stream_data_size);
        }
        if (send_success && send_ctx.level == kApplication && key_update_trigger_.IsEnabled()) {
            if (key_update_trigger_.OnBytesSent(result.packet_size)) {
                if (connection_crypto_.TriggerKeyUpdate()) {
                    key_update_trigger_.MarkTriggered();
                    key_update_trigger_.Reset();
                    LOG_INFO("Key Update triggered after sending %u bytes", result.packet_size);
                }
            }
        }

        if (!send_success) {
            // SendBuffer already accounted the failure; stop the burst
            // so the worker can decide whether to drop the conn or retry.
            break;
        }
        ++sent;
    }

    return sent;
}

int BaseConnection::TryCoalescedInitialHandshake() {
    // 1. Quick applicability check.
    //
    // We need both Initial and Handshake to (a) have install cryptographers
    // and (b) have CRYPTO bytes pending in their per-level send queues.
    // The cryptographer check naturally screens out the post-Initial-discard
    // window (RFC 9000 §4.10): once Initial keys are dropped, GetCryptographer
    // returns nullptr and we fall back to the normal single-level path.
    auto initial_crypto = connection_crypto_.GetCryptographer(kInitial);
    auto handshake_crypto = connection_crypto_.GetCryptographer(kHandshake);
    if (!initial_crypto || !handshake_crypto) {
        return 0;
    }

    auto crypto_stream = connection_crypto_.GetCryptoStream();
    if (!crypto_stream) {
        return 0;
    }
    bool init_pending = crypto_stream->HasPendingDataAt(kInitial);
    bool hs_pending = crypto_stream->HasPendingDataAt(kHandshake);
    if (!init_pending || !hs_pending) {
        return 0;
    }

    // 2. cwnd budget. Coalescing emits >=1200 B on the client first flight
    // (anti-amplification padding) and a bit less on the server first
    // flight. If cwnd cannot cover at least the padded minimum we bail and
    // let the regular single-level burst try a smaller packet that fits.
    //
    // RFC 9002 explicitly allows the *initial* Initial to bypass cwnd, but
    // we already implement that via the SendControl initial-allowance hook
    // (see GetAvailableWindow). For subsequent rounds this check correctly
    // gates by current cwnd.
    uint32_t max_bytes = send_manager_.GetAvailableWindow();
    if (max_bytes == 0) {
        // Anti-amplification probe exemption isn't relevant here (no
        // PATH_CHALLENGE during initial handshake). Just bail.
        send_manager_.SetCwndLimited();
        return 0;
    }

    // 3. Build Initial first into the (empty) shared buffer.
    //
    // Padding is intentionally *disabled* for the Initial packet here.
    // Per RFC 9000 §14.1 + §12.2 the 1200 B rule applies to the whole
    // datagram; we will add the padding to the trailing Handshake packet
    // instead so the Initial stays as small as possible (cheaper for the
    // peer's Initial-keys AEAD path, and lets us build packets in natural
    // on-wire order without a back-patch dance).
    auto chunk = std::make_shared<common::BufferChunk>(quic::GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("BaseConnection::TryCoalescedInitialHandshake: failed to allocate chunk");
        return 0;
    }
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);

    // qlog draft-03: this is the canonical packet-coalescing path. Open
    // ONE datagram_id and let both the Initial and Handshake builds share
    // it so qvis can render them as a single UDP datagram (which is the
    // whole point of the visualisation upgrade). The Scope guarantees the
    // bracket closes on every one of the early returns below.
    auto scope = emitter_->Open();

    PacketBuilder::DataPacketContext init_ctx;
    FillPacketIdentity(init_ctx, kInitial, initial_crypto);
    init_ctx.key_phase = 0;               // long headers carry no key-phase bit
    init_ctx.include_stream_data = true;  // CryptoStream drains via stream-frame path
    init_ctx.add_padding = false;         // padding deferred to Handshake (see above)
    init_ctx.min_size = 0;
    init_ctx.token = send_manager_.GetToken();
    init_ctx.max_stream_data_size = max_bytes;
    init_ctx.frames = send_manager_.GetPendingFrames(kInitial, max_bytes);

    // Piggyback any pending Initial-space ACK (cross-level ACKs are
    // already correctly routed by the scheduler in the non-coalesced
    // path; here we just emit our own-space ACK if RecvControl says it's
    // due — same as the normal burst path).
    MaybePrependAck(init_ctx.frames, kInitialNumberSpace);

    auto init_result = packet_builder_->BuildDataPacket(
        init_ctx, buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl());
    if (!init_result.success) {
        LOG_WARN("BaseConnection::TryCoalescedInitialHandshake: Initial build failed: %s",
            init_result.error_message.c_str());
        // Scope's destructor discards the empty datagram bracket.
        return 0;
    }
    const uint32_t initial_size = init_result.packet_size;
    LOG_DEBUG("BaseConnection::TryCoalescedInitialHandshake: built Initial pn=%llu size=%u", init_result.packet_number,
        initial_size);

    // 4. Compute padding target for the Handshake packet.
    //
    // RFC 9000 §14.1: client carrying-Initial datagrams MUST be >= 1200 B.
    // The server has no such requirement, but we still benefit from
    // coalescing (one syscall, one round-trip).
    //
    // The PacketBuilder's `min_size` field is compared against the
    // *plaintext payload* length inside the visitor (see step 6 of
    // BuildDataPacket). The on-wire Handshake packet additionally
    // contributes a long-header (~13 B lower bound) + AEAD tag (16 B) =
    // ~29 B of envelope overhead on top of the plaintext. To translate
    // "datagram total >= 1200 B" into a plaintext lower bound we
    // subtract a conservative envelope estimate. Erring high (29 B) is
    // safe: it may make the datagram a few bytes over 1200 (still fully
    // compliant; §14.1 has no upper bound below MTU) but never under.
    const bool is_client = !is_server_;
    const bool client_first_flight = is_client && !encryption_scheduler_->IsInitialPacketSent();
    const uint32_t target_datagram_size = client_first_flight ? kMinInitialPacketSize : 0;
    constexpr uint32_t kHandshakeEnvelopeEstimate = 29;  // header lower bound + AEAD tag

    uint32_t handshake_min_plaintext = 0;
    if (target_datagram_size > 0 && target_datagram_size > initial_size + kHandshakeEnvelopeEstimate) {
        handshake_min_plaintext = target_datagram_size - initial_size - kHandshakeEnvelopeEstimate;
    }

    // 5. Build Handshake, *appended* to the same buffer.
    //
    // The pre_size snapshot added to BuildDataPacket means SendControl
    // will see the Handshake's own packet size (not the combined
    // datagram length) when OnPacketSend records bytes-in-flight.
    PacketBuilder::DataPacketContext hs_ctx;
    FillPacketIdentity(hs_ctx, kHandshake, handshake_crypto);
    hs_ctx.key_phase = 0;  // long headers carry no key-phase bit
    hs_ctx.include_stream_data = true;
    hs_ctx.add_padding = (handshake_min_plaintext > 0);
    hs_ctx.min_size = handshake_min_plaintext;
    hs_ctx.max_stream_data_size = max_bytes;  // separate per-level cap
    hs_ctx.frames = send_manager_.GetPendingFrames(kHandshake, max_bytes);

    MaybePrependAck(hs_ctx.frames, kHandshakeNumberSpace);

    auto hs_result = packet_builder_->BuildDataPacket(
        hs_ctx, buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl());
    if (!hs_result.success) {
        // Initial has already been built into the buffer and recorded by
        // SendControl. Two options at this point:
        //   (a) Send what we have — Initial alone — accepting it lacks
        //       padding (if client_first_flight, that violates §14.1).
        //   (b) Send anyway only if padding wasn't required; otherwise
        //       drop and let the legacy path rebuild Initial with padding.
        //
        // We take a third, simpler stance: if Handshake build fails,
        // send the Initial as-is *only* when no padding was required
        // (server or non-first-flight). For the client first flight,
        // we cannot safely send an under-1200 datagram, so we report 0
        // and rely on the normal level-sticky burst (next iteration) to
        // rebuild the Initial with padding. The cost of this fallback
        // is one wasted packet-number on the Initial — acceptable
        // because handshake build failures are rare paths.
        LOG_WARN(
            "BaseConnection::TryCoalescedInitialHandshake: Handshake build failed: %s "
            "(initial_size=%u, client_first_flight=%d)",
            hs_result.error_message.c_str(), initial_size, client_first_flight ? 1 : 0);
        if (client_first_flight) {
            // Discard the buffer; caller's TrySendNewBurst will rebuild
            // and pad Initial properly on the next pass.
            // qlog draft-03: the Initial we just built is being thrown
            // away — the Scope destructor drains the datagram bracket so
            // the next attempt gets a fresh id rather than inheriting this
            // orphan one.
            return 0;
        }
        // Server or non-first-flight: ship Initial alone — the datagram
        // therefore holds exactly one packet (the Initial).
        encryption_scheduler_->SetInitialPacketSent(true);
        if (!scope.Commit(buffer)) {
            return 0;
        }
        return 1;
    }

    LOG_DEBUG(
        "BaseConnection::TryCoalescedInitialHandshake: built Handshake pn=%llu size=%u, "
        "datagram_total=%u (target>=%u, client_first_flight=%d)",
        hs_result.packet_number, hs_result.packet_size, initial_size + hs_result.packet_size, target_datagram_size,
        client_first_flight ? 1 : 0);

    // 6. Update Initial-sent flag (scheduler uses this for 0-RTT ordering).
    encryption_scheduler_->SetInitialPacketSent(true);

    // No connection-level flow-control accounting here, unlike TrySendNewBurst.
    // That asymmetry is correct, not an omission: send_flow_controller_ counts
    // STREAM bytes, and this path cannot emit any. StreamManager::BuildStreamFrames
    // skips every non-CryptoStream unless the level is kEarlyData or kApplication
    // (RFC 9000 §12.5), so at Initial/Handshake only CRYPTO frames are produced —
    // and the byte counter behind result.stream_data_size is only advanced by
    // SendStream, which CryptoStream does not derive from. The value is therefore
    // always 0 here and OnDataSent() would be a no-op.

    // 7. Ship the coalesced datagram.
    if (!scope.Commit(buffer)) {
        // The emitter already accounted the failure; both packets are
        // already in the SendControl bytes-in-flight ledger and will be
        // detected as lost via the normal PTO path. Nothing more to do.
        return 0;
    }
    common::Metrics::HistogramObserve(common::MetricsStd::DiagPktPayloadHist, buffer->GetDataLength());
    return 2;
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

    // qlog draft-03: ACK-only datagram carries exactly one QUIC packet.
    auto scope = emitter_->Open();

    auto result = packet_builder_->BuildAckPacket(target_level, cryptographer, ack_frame,
        cid_coordinator_->GetLocalConnectionIDManager().get(), cid_coordinator_->GetRemoteConnectionIDManager().get(),
        buffer, send_manager_.GetPacketNumber(), send_manager_.GetSendControl(), connection_crypto_.GetVersion(),
        connection_crypto_.GetCurrentKeyPhase());

    if (!result.success) {
        LOG_ERROR("BaseConnection::SendImmediateAck: failed to build packet: %s", result.error_message.c_str());
        // Scope's destructor discards the empty datagram bracket.
        return false;
    }

    LOG_DEBUG("BaseConnection::SendImmediateAck: built ACK packet pn=%llu, size=%u", result.packet_number,
        result.packet_size);

    // 5. Send immediately: bypass the batch sink so the ACK does not wait for
    // the end-of-round sendmmsg flush.
    return scope.Commit(buffer, /*bypass_batch=*/true);
}

}  // namespace quic
}  // namespace quicx