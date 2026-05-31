#include "common/buffer/buffer_chunk.h"
#include "common/log/log.h"
#include "common/log/log_context.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>

#include "quic/common/version.h"
#include "quic/config.h"
#include "quic/connection/connection_id.h"
#include "quic/connection/connection_id_generator.h"
#include "quic/connection/connection_server.h"
#include "quic/connection/error.h"
#include "quic/crypto/retry_crypto.h"
#include "quic/crypto/type.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/version_negotiation_packet.h"
#include "quic/quicx/global_resource.h"
#include "quic/quicx/worker_server.h"

namespace quicx {
namespace quic {

ServerWorker::ServerWorker(const QuicServerConfig& config, std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
    const QuicTransportParams& params, connection_state_callback connection_handler,
    std::shared_ptr<common::IEventLoop> event_loop):
    Worker(config.config_, ctx, sender, params, connection_handler, event_loop),
    server_alpn_(config.alpn_),
    retry_policy_(config.retry_policy_),
    selective_config_(config.selective_retry_config_),
    retry_token_lifetime_(config.retry_token_lifetime_) {
    // Initialize Retry infrastructure based on policy
    if (retry_policy_ != RetryPolicy::NEVER) {
        retry_token_manager_ = std::make_shared<RetryTokenManager>();

        // Initialize rate monitoring components for SELECTIVE mode
        if (retry_policy_ == RetryPolicy::SELECTIVE) {
            rate_monitor_ = std::make_shared<ConnectionRateMonitor>(event_loop);
            ip_limiter_ = std::make_shared<IPRateLimiter>(selective_config_.ip_cache_size_,
                selective_config_.ip_rate_threshold_, selective_config_.ip_window_seconds_);
            LOG_INFO(
                "Retry mechanism enabled (SELECTIVE mode). rate_threshold=%u, ip_threshold=%u, token_lifetime=%u",
                selective_config_.rate_threshold_, selective_config_.ip_rate_threshold_, retry_token_lifetime_);
        } else {
            LOG_INFO("Retry mechanism enabled (ALWAYS mode). token_lifetime=%u", retry_token_lifetime_);
        }
    } else {
        LOG_INFO("Retry mechanism disabled (NEVER mode)");
    }
}

ServerWorker::~ServerWorker() {}

void ServerWorker::Shutdown() {
    // Precondition (enforced by QuicServer::~QuicServer): the worker's
    // event-loop thread has already been Stop()+Join()'d. The loop is
    // therefore guaranteed not to fire any pending timer — and it is *not*
    // safe to invoke EventLoop::RemoveTimer / AddTimer from this thread
    // because AssertInLoopThread() would abort.
    //
    // We just drop the bookkeeping containers; each handshake timer's
    // captured shared_ptr<ServerConnection> is released via ~unordered_map,
    // and ConnectionRateMonitor's owned timer-id becomes irrelevant once
    // the EventLoop tears down its timer wheel during destruction.
    handshake_timers_.clear();

    // Drop retry / rate-monitoring helpers. ConnectionRateMonitor owns a
    // weak_ptr to the event loop; ~ConnectionRateMonitor will attempt to
    // StopTimer() but lock() will fail once master_event_loop_ is gone, so
    // it is a no-op in the worst case. We deliberately do *not* call
    // StopTimer() here (it would call EventLoop::RemoveTimer from the
    // wrong thread → AssertInLoopThread() abort).
    rate_monitor_.reset();
    ip_limiter_.reset();
    retry_token_manager_.reset();

    Worker::Shutdown();
}

bool ServerWorker::ShouldSendRetry(bool has_valid_token, const common::Address& client_addr) {
    // If client already has a valid token, never send another Retry (RFC 9000 requirement)
    if (has_valid_token) {
        return false;
    }

    switch (retry_policy_) {
        case RetryPolicy::NEVER:
            // Performance priority: never send Retry
            return false;

        case RetryPolicy::ALWAYS:
            // Security priority: always send Retry for new connections without token
            common::Metrics::CounterInc(common::MetricsStd::QuicRetryByPolicy);
            return true;

        case RetryPolicy::SELECTIVE: {
            // Smart decision based on server load and IP behavior

            // Check 1: High connection rate (server under load)
            if (rate_monitor_ && rate_monitor_->IsHighRate(selective_config_.rate_threshold_)) {
                LOG_DEBUG("ShouldSendRetry: high connection rate detected, sending Retry");
                common::Metrics::CounterInc(common::MetricsStd::QuicRetryByHighRate);
                return true;
            }

            // Check 2: Suspicious IP (potential attack)
            if (ip_limiter_ && ip_limiter_->IsSuspicious(client_addr)) {
                LOG_DEBUG("ShouldSendRetry: suspicious IP %s, sending Retry", client_addr.GetIp().c_str());
                common::Metrics::CounterInc(common::MetricsStd::QuicRetryBySuspiciousIP);
                return true;
            }

            // Normal conditions: accept connection directly (better performance)
            return false;
        }

        default:
            LOG_WARN("ShouldSendRetry: unknown retry policy, defaulting to no Retry");
            return false;
    }
}

bool ServerWorker::InnerHandlePacket(PacketParseResult& packet_info) {
    if (packet_info.packets_.empty()) {
        LOG_ERROR("get a netpacket, but data packets is empty");
        return false;
    }

    // dispatch packet
    LOG_DEBUG("get packet. dcid:%llu", packet_info.cid_.Hash());
    auto conn = conn_map_.find(packet_info.cid_.Hash());
    // Per-datagram trace: keep at DEBUG. Under load this fires once per
    // received packet (millions of times in a benchmark) and at INFO it
    // synchronously stalls the worker on log-flush IO -- which presents
    // as a sudden ~9s "stall" mid-test even though no real progress is
    // being lost. See note in connection_base.cpp::ActiveSend.
    LOG_DEBUG("[DISPATCH-TRACE] dispatch dcid_hash=%llu hit=%d conn_map=%zu connecting_set=%zu hs_timers=%zu",
        packet_info.cid_.Hash(), conn != conn_map_.end() ? 1 : 0,
        conn_map_.size(), connecting_set_.size(), handshake_timers_.size());
    if (conn != conn_map_.end()) {
        common::LogTagGuard guard("conn:" + std::to_string(packet_info.cid_.Hash()));
        // Pin the connection with a local shared_ptr copy. OnPackets may
        // trigger OnStateToDraining → InvokeConnectionCloseCallback which
        // removes the entry from conn_map_. Without this pin, the iterator
        // would be the last owner and `this` would be destroyed mid-call.
        auto connection = conn->second;
        connection->SetSocket(packet_info.net_packet_->GetSocket());
        // report observed address for path change detection
        auto& observed_addr = packet_info.net_packet_->GetAddress();
        connection->OnObservedPeerAddress(observed_addr);
        // record received bytes on candidate path to unlock anti-amplification budget
        if (packet_info.net_packet_->GetData()) {
            connection->OnCandidatePathDatagramReceived(
                observed_addr, packet_info.net_packet_->GetData()->GetDataLength());
        }
        connection->SetPendingEcn(packet_info.net_packet_->GetEcn());
        connection->OnPackets(packet_info.net_packet_->GetTime(), packet_info.packets_);
        return true;
    }

    // check init packet
    // Pass the original UDP datagram size for RFC 9000 §14.1 minimum size check
    // datagram_size_ is saved by MsgParser::ParsePacket() before DecodePackets consumes the buffer
    if (!InitPacketCheck(packet_info.packets_[0], packet_info.datagram_size_)) {
        LOG_ERROR("init packet check failed");
        // Extract DCID/SCID from the received packet for the VN response
        auto* hdr = static_cast<LongHeader*>(packet_info.packets_[0]->GetHeader());
        SendVersionNegotiatePacket(packet_info.net_packet_->GetAddress(), packet_info.net_packet_->GetSocket(),
            hdr->GetDestinationConnectionId(), hdr->GetDestinationConnectionIdLength(),
            hdr->GetSourceConnectionId(), hdr->GetSourceConnectionIdLength());
        return false;
    }

    auto init_packet = packet_info.packets_[0];
    auto long_header = static_cast<LongHeader*>(init_packet->GetHeader());
    if (long_header == nullptr) {
        LOG_ERROR("long header is nullptr");
        return false;
    }
    ConnectionID src_cid(long_header->GetSourceConnectionId(), long_header->GetSourceConnectionIdLength());
    ConnectionID dst_cid(long_header->GetDestinationConnectionId(), long_header->GetDestinationConnectionIdLength());
    common::LogTagGuard guard("conn:" + std::to_string(dst_cid.Hash()));

    // Record this connection attempt for rate monitoring
    const auto& client_addr = packet_info.net_packet_->GetAddress();
    if (rate_monitor_) {
        rate_monitor_->RecordNewConnection();
    }
    if (ip_limiter_) {
        ip_limiter_->RecordConnection(client_addr);
    }

    // Check if Retry is needed based on policy
    bool retry_was_used = false;
    ConnectionID original_dcid;  // The DCID from the client's very first Initial (before Retry)
    if (retry_policy_ != RetryPolicy::NEVER) {
        // Check if this Initial packet contains a valid Retry token
        auto init_pkt = std::dynamic_pointer_cast<InitPacket>(init_packet);
        if (init_pkt) {
            uint8_t* token_data = init_pkt->GetToken();
            uint32_t token_len = init_pkt->GetTokenLength();
            bool has_valid_token = false;

            if (token_data != nullptr && token_len > 0) {
                // Validate the token and extract ODCID
                std::string token((char*)token_data, token_len);
                ConnectionID odcid;
                has_valid_token = ValidateRetryToken(token, client_addr, odcid);

                if (has_valid_token) {
                    LOG_DEBUG("Valid Retry token received. ODCID extracted: %llu", odcid.Hash());
                    common::Metrics::CounterInc(common::MetricsStd::QuicRetryTokensValidated);
                    // RFC 9000 §7.3: After Retry, the server MUST set
                    // original_destination_connection_id to the DCID from the
                    // client's very first Initial (extracted from the token),
                    // and retry_source_connection_id to the SCID chosen by the
                    // server in its Retry packet (which is now the DCID in the
                    // post-Retry Initial from the client, i.e., dst_cid).
                    retry_was_used = true;
                    original_dcid = odcid;
                } else {
                    LOG_WARN("Invalid Retry token received");
                    common::Metrics::CounterInc(common::MetricsStd::QuicRetryTokensInvalid);
                }
            }

            // Use policy-based decision for Retry
            if (ShouldSendRetry(has_valid_token, client_addr)) {
                LOG_INFO("Sending Retry packet to client (policy=%d)", static_cast<int>(retry_policy_));
                uint32_t client_version = long_header->GetVersion();
                if (SendRetryPacket(client_addr, packet_info.net_packet_->GetSocket(), dst_cid, src_cid, client_version)) {
                    common::Metrics::CounterInc(common::MetricsStd::QuicRetryPacketsSent);
                    return true;  // Retry sent, don't create connection yet
                } else {
                    LOG_ERROR("Failed to send Retry packet");
                    // Fall through to create connection anyway
                }
            }
        }
    }

    // create new connection
    ConnectionCallbacks callbacks;
    callbacks.active_connection_cb = std::bind(&ServerWorker::HandleActiveSendConnection, this, std::placeholders::_1);
    callbacks.handshake_done_cb = std::bind(&ServerWorker::HandleHandshakeDone, this, std::placeholders::_1);
    callbacks.add_conn_id_cb = std::bind(&ServerWorker::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2);
    callbacks.retire_conn_id_cb = std::bind(&ServerWorker::HandleRetireConnectionId, this, std::placeholders::_1);
    callbacks.connection_close_cb = std::bind(&ServerWorker::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3);

    auto new_conn = std::make_shared<ServerConnection>(ctx_, event_loop_.lock(), server_alpn_, callbacks);

    // Inject Sender for direct packet transmission
    new_conn->SetSender(sender_);

    // RFC 9368 Compatible Version Negotiation: on the server side, the on-wire
    // |quic_version_| is determined by the client's Initial packet (set in
    // BaseConnection::OnInitialPacket). The configured |quic_version_| here
    // expresses the server operator's *preferred* version: if the client's
    // available_versions list includes it (and it differs from the client's
    // chosen_version) we will compatibly upgrade during TP processing.
    new_conn->SetPreferredVersion(quic_version_);

    // RFC 9000 §18.2: Server MUST include original_destination_connection_id
    // in transport parameters, set to the DCID from the client's first Initial packet.
    QuicTransportParams server_params = params_;
    if (retry_was_used) {
        // After Retry: ODCID comes from the validated token (the client's very first DCID),
        // and retry_source_connection_id is the SCID the server chose in its Retry packet
        // (which the client now uses as DCID in post-Retry Initial, i.e., dst_cid).
        server_params.original_destination_connection_id_ =
            std::string(reinterpret_cast<const char*>(original_dcid.GetID()), original_dcid.GetLength());
        server_params.retry_source_connection_id_ =
            std::string(reinterpret_cast<const char*>(dst_cid.GetID()), dst_cid.GetLength());
        LOG_INFO("Retry was used: ODCID from token (hash=%llu), retry_scid=dst_cid (hash=%llu)",
            original_dcid.Hash(), dst_cid.Hash());
    } else {
        // No Retry: ODCID is the DCID from the client's Initial packet
        server_params.original_destination_connection_id_ =
            std::string(reinterpret_cast<const char*>(dst_cid.GetID()), dst_cid.GetLength());
    }
    new_conn->AddTransportParam(server_params);
    connecting_set_.insert(new_conn);

    // Register Initial DCID to connection map so subsequent packets can be routed
    conn_map_[dst_cid.Hash()] = new_conn;

    LOG_INFO("[DISPATCH-TRACE] new_conn dcid_hash=%llu scid_hash=%llu conn=%p retry=%d "
             "conn_map=%zu connecting_set=%zu peer=%s",
        dst_cid.Hash(), src_cid.Hash(), (void*)new_conn.get(), retry_was_used ? 1 : 0,
        conn_map_.size(), connecting_set_.size(),
        packet_info.net_packet_->GetAddress().AsString().c_str());

    // add remote connection id
    new_conn->AddRemoteConnectionId(src_cid);
    new_conn->SetSocket(packet_info.net_packet_->GetSocket());
    new_conn->SetPeerAddress(packet_info.net_packet_->GetAddress());
    new_conn->SetPendingEcn(packet_info.net_packet_->GetEcn());
    new_conn->OnPackets(packet_info.net_packet_->GetTime(), packet_info.packets_);

    // Handshake watchdog timer: if the handshake does not finish within
    // kHandshakeTimeoutMs we tear the connection down.
    //
    // IMPORTANT: the lambda captures |new_conn| by value, so as long as the
    // timer sits in the event loop it holds a shared_ptr<ServerConnection>
    // and keeps the connection object alive. We therefore remember the
    // timer id and cancel it in HandleHandshakeDone() the moment the
    // handshake succeeds; otherwise every short-lived benchmark cycle would
    // leak ~120 KB of per-connection state for the full 5 s timeout window,
    // which is the P4 residue observed in profile_rss_lifecycle.
    auto hs_loop = event_loop_.lock();
    if (!hs_loop) return false;
    uint64_t timer_id = hs_loop->AddTimer(
        [new_conn, this]() {
            if (connecting_set_.find(new_conn) != connecting_set_.end()) {
                LOG_INFO("[DISPATCH-TRACE] watchdog_fire conn=%p scid_hash=%llu "
                         "conn_map=%zu connecting_set=%zu",
                    (void*)new_conn.get(), new_conn->GetConnectionIDHash(),
                    conn_map_.size(), connecting_set_.size());
                LOG_DEBUG("connection timeout during handshake. cid:%llu", new_conn->GetConnectionIDHash());
                // Properly close the connection to clean up all CIDs
                HandleConnectionClose(new_conn, QuicErrorCode::kNoError, "handshake timeout");
            }
            handshake_timers_.erase(new_conn);
        },
        kHandshakeTimeoutMs);
    handshake_timers_[new_conn] = timer_id;
    return true;
}

void ServerWorker::HandleHandshakeDone(std::shared_ptr<IConnection> conn) {
    // Cancel the handshake watchdog timer registered in InnerHandlePacket()
    // so the captured shared_ptr<ServerConnection> is released and the
    // connection can be destroyed as soon as it is closed, instead of
    // outliving itself by up to kHandshakeTimeoutMs.
    auto it = handshake_timers_.find(conn);
    if (it != handshake_timers_.end()) {
        auto done_loop = event_loop_.lock();
        if (done_loop) done_loop->RemoveTimer(it->second);
        handshake_timers_.erase(it);
        LOG_DEBUG(
            "ServerWorker: handshake completed, cancelled watchdog timer for cid:%llu", conn->GetConnectionIDHash());
    }
    Worker::HandleHandshakeDone(conn);
}

bool ServerWorker::SendRetryPacket(
    const common::Address& addr, int32_t socket, const ConnectionID& original_dcid, const ConnectionID& original_scid,
    uint32_t version) {
    if (!retry_token_manager_) {
        LOG_ERROR("Retry token manager not initialized");
        return false;
    }

    // Generate a new server connection ID for this Retry
    uint8_t new_scid_data[kRetryCidLength];
    ConnectionIDGenerator::Instance().Generator(new_scid_data, kRetryCidLength);
    ConnectionID new_scid(new_scid_data, kRetryCidLength);

    // Generate Retry token
    std::string token = retry_token_manager_->GenerateToken(addr, original_dcid);
    if (token.empty()) {
        LOG_ERROR("Failed to generate Retry token");
        return false;
    }

    // Build Retry packet
    RetryPacket retry_packet;
    auto& header = *static_cast<LongHeader*>(retry_packet.GetHeader());

    // Set header fields - use the version from client's Initial packet
    header.SetVersion(version);
    header.SetSourceConnectionId(new_scid.GetID(), new_scid.GetLength());
    header.SetDestinationConnectionId(original_scid.GetID(), original_scid.GetLength());

    // Allocate buffer for token and copy data
    auto token_chunk = std::make_shared<common::BufferChunk>(GlobalResource::Instance().GetThreadLocalBlockPool());
    if (!token_chunk || !token_chunk->Valid()) {
        LOG_ERROR("Failed to allocate token buffer");
        return false;
    }
    uint8_t* token_start = token_chunk->GetData();
    memcpy(token_start, token.data(), token.size());
    auto token_span = common::SharedBufferSpan(token_chunk, token_start, token_start + token.size());
    retry_packet.SetRetryToken(token_span);

    // Compute Retry Integrity Tag (RFC 9001 Section 5.8)
    // Step 1: Encode retry packet without tag to get the packet body
    std::shared_ptr<NetPacket> temp_pkt = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    auto temp_buffer = temp_pkt->GetData();
    
    // Set a placeholder tag first for encoding
    uint8_t placeholder_tag[kRetryIntegrityTagLength] = {0};
    retry_packet.SetRetryIntegrityTag(placeholder_tag);
    if (!retry_packet.Encode(temp_buffer)) {
        LOG_ERROR("Failed to encode Retry packet for integrity tag");
        return false;
    }
    
    // Get encoded packet body (without the 16-byte tag at the end)
    auto data_span = temp_buffer->GetReadableSpan();
    uint32_t encoded_len = data_span.GetLength();
    uint32_t retry_body_len = encoded_len - kRetryIntegrityTagLength;
    
    // Step 2: Compute integrity tag using crypto module
    uint8_t integrity_tag[kRetryIntegrityTagLength];
    if (!RetryCrypto::ComputeRetryIntegrityTag(
            original_dcid, data_span.GetStart(), retry_body_len, version, integrity_tag)) {
        LOG_ERROR("Failed to compute Retry integrity tag");
        return false;
    }
    
    // Step 3: Set the computed tag and re-encode
    retry_packet.SetRetryIntegrityTag(integrity_tag);

    // Encode and send
    std::shared_ptr<NetPacket> net_packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    auto buffer = net_packet->GetData();

    if (!retry_packet.Encode(buffer)) {
        LOG_ERROR("Failed to encode Retry packet");
        return false;
    }

    net_packet->SetAddress(addr);
    net_packet->SetSocket(socket);
    sender_->Send(net_packet);

    LOG_INFO("Sent Retry packet. new_scid:%llu, token_len:%zu", new_scid.Hash(), token.size());
    return true;
}

bool ServerWorker::ValidateRetryToken(
    const std::string& token, const common::Address& addr, ConnectionID& out_original_dcid) {
    if (!retry_token_manager_) {
        return false;
    }
    return retry_token_manager_->ValidateToken(token, addr, out_original_dcid, retry_token_lifetime_);
}

void ServerWorker::SendVersionNegotiatePacket(const common::Address& addr, int32_t socket,
    const uint8_t* client_dcid, uint8_t client_dcid_len,
    const uint8_t* client_scid, uint8_t client_scid_len) {
    VersionNegotiationPacket version_negotiation_packet;

    // RFC 9000 §17.2.1: echo client's SCID as VN's DCID, client's DCID as VN's SCID
    auto* header = static_cast<LongHeader*>(version_negotiation_packet.GetHeader());
    header->SetDestinationConnectionId(client_scid, client_scid_len);
    header->SetSourceConnectionId(client_dcid, client_dcid_len);

    for (auto version : kQuicVersions) {
        version_negotiation_packet.AddSupportVersion(version);
    }

    std::shared_ptr<NetPacket> net_packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    auto buffer = net_packet->GetData();
    version_negotiation_packet.Encode(buffer);

    net_packet->SetAddress(addr);
    net_packet->SetSocket(socket);
    sender_->Send(net_packet);
    LOG_DEBUG("send version negotiate packet. packet size:%d", buffer->GetDataLength());
}

void ServerWorker::HandleConnectionClose(
    std::shared_ptr<IConnection> conn, uint64_t error, const std::string& reason) {
    // Also purge the handshake watchdog entry: if the connection is closed
    // before its handshake completes (peer aborted, handshake error, etc.)
    // there may still be a pending timer whose lambda owns a shared_ptr to
    // the connection. Removing it releases that last reference so the
    // connection object can actually be destroyed.
    auto it = handshake_timers_.find(conn);
    if (it != handshake_timers_.end()) {
        auto close_loop = event_loop_.lock();
        if (close_loop) close_loop->RemoveTimer(it->second);
        handshake_timers_.erase(it);
    }
    Worker::HandleConnectionClose(conn, error, reason);
}

}  // namespace quic
}  // namespace quicx