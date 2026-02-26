#include "common/buffer/buffer_chunk.h"
#include "common/log/log.h"
#include "common/log/log_context.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

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
            common::LOG_INFO(
                "Retry mechanism enabled (SELECTIVE mode). rate_threshold=%u, ip_threshold=%u, token_lifetime=%u",
                selective_config_.rate_threshold_, selective_config_.ip_rate_threshold_, retry_token_lifetime_);
        } else {
            common::LOG_INFO("Retry mechanism enabled (ALWAYS mode). token_lifetime=%u", retry_token_lifetime_);
        }
    } else {
        common::LOG_INFO("Retry mechanism disabled (NEVER mode)");
    }
}

ServerWorker::~ServerWorker() {}

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
                common::LOG_DEBUG("ShouldSendRetry: high connection rate detected, sending Retry");
                common::Metrics::CounterInc(common::MetricsStd::QuicRetryByHighRate);
                return true;
            }

            // Check 2: Suspicious IP (potential attack)
            if (ip_limiter_ && ip_limiter_->IsSuspicious(client_addr)) {
                common::LOG_DEBUG("ShouldSendRetry: suspicious IP %s, sending Retry", client_addr.GetIp().c_str());
                common::Metrics::CounterInc(common::MetricsStd::QuicRetryBySuspiciousIP);
                return true;
            }

            // Normal conditions: accept connection directly (better performance)
            return false;
        }

        default:
            common::LOG_WARN("ShouldSendRetry: unknown retry policy, defaulting to no Retry");
            return false;
    }
}

bool ServerWorker::InnerHandlePacket(PacketParseResult& packet_info) {
    if (packet_info.packets_.empty()) {
        common::LOG_ERROR("get a netpacket, but data packets is empty");
        return false;
    }

    // dispatch packet
    common::LOG_DEBUG("get packet. dcid:%llu", packet_info.cid_.Hash());
    auto conn = conn_map_.find(packet_info.cid_.Hash());
    if (conn != conn_map_.end()) {
        common::LogTagGuard guard("conn:" + std::to_string(packet_info.cid_.Hash()));
        conn->second->SetSocket(packet_info.net_packet_->GetSocket());
        // report observed address for path change detection
        auto& observed_addr = packet_info.net_packet_->GetAddress();
        conn->second->OnObservedPeerAddress(observed_addr);
        // record received bytes on candidate path to unlock anti-amplification budget
        if (packet_info.net_packet_->GetData()) {
            conn->second->OnCandidatePathDatagramReceived(
                observed_addr, packet_info.net_packet_->GetData()->GetDataLength());
        }
        conn->second->SetPendingEcn(packet_info.net_packet_->GetEcn());
        conn->second->OnPackets(packet_info.net_packet_->GetTime(), packet_info.packets_);
        return true;
    }

    // check init packet
    // Pass the original UDP datagram size for RFC 9000 §14.1 minimum size check
    // datagram_size_ is saved by MsgParser::ParsePacket() before DecodePackets consumes the buffer
    if (!InitPacketCheck(packet_info.packets_[0], packet_info.datagram_size_)) {
        common::LOG_ERROR("init packet check failed");
        SendVersionNegotiatePacket(packet_info.net_packet_->GetAddress(), packet_info.net_packet_->GetSocket());
        return false;
    }

    auto init_packet = packet_info.packets_[0];
    auto long_header = static_cast<LongHeader*>(init_packet->GetHeader());
    if (long_header == nullptr) {
        common::LOG_ERROR("long header is nullptr");
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
                    common::LOG_DEBUG("Valid Retry token received. ODCID extracted: %llu", odcid.Hash());
                    common::Metrics::CounterInc(common::MetricsStd::QuicRetryTokensValidated);
                    // TODO: Set ODCID on connection context for transport parameters
                } else {
                    common::LOG_WARN("Invalid Retry token received");
                    common::Metrics::CounterInc(common::MetricsStd::QuicRetryTokensInvalid);
                }
            }

            // Use policy-based decision for Retry
            if (ShouldSendRetry(has_valid_token, client_addr)) {
                common::LOG_INFO("Sending Retry packet to client (policy=%d)", static_cast<int>(retry_policy_));
                uint32_t client_version = long_header->GetVersion();
                if (SendRetryPacket(client_addr, packet_info.net_packet_->GetSocket(), dst_cid, src_cid, client_version)) {
                    common::Metrics::CounterInc(common::MetricsStd::QuicRetryPacketsSent);
                    return true;  // Retry sent, don't create connection yet
                } else {
                    common::LOG_ERROR("Failed to send Retry packet");
                    // Fall through to create connection anyway
                }
            }
        }
    }

    // create new connection
    auto new_conn = std::make_shared<ServerConnection>(ctx_, event_loop_, server_alpn_,
        std::bind(&ServerWorker::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&ServerWorker::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&ServerWorker::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ServerWorker::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&ServerWorker::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3));

    // Inject Sender for direct packet transmission
    new_conn->SetSender(sender_);

    // Set QUIC version from configuration
    new_conn->SetVersion(quic_version_);

    new_conn->AddTransportParam(params_);
    connecting_set_.insert(new_conn);

    // Register Initial DCID to connection map so subsequent packets can be routed
    conn_map_[dst_cid.Hash()] = new_conn;

    // add remote connection id
    new_conn->AddRemoteConnectionId(src_cid);
    new_conn->SetSocket(packet_info.net_packet_->GetSocket());
    new_conn->SetPeerAddress(packet_info.net_packet_->GetAddress());
    new_conn->SetPendingEcn(packet_info.net_packet_->GetEcn());
    new_conn->OnPackets(packet_info.net_packet_->GetTime(), packet_info.packets_);

    event_loop_->AddTimer(
        [new_conn, this]() {
            if (connecting_set_.find(new_conn) != connecting_set_.end()) {
                common::LOG_DEBUG("connection timeout during handshake. cid:%llu", new_conn->GetConnectionIDHash());
                // Properly close the connection to clean up all CIDs
                HandleConnectionClose(new_conn, QuicErrorCode::kNoError, "handshake timeout");
            }
        },
        kHandshakeTimeoutMs);
    return true;
}

bool ServerWorker::SendRetryPacket(
    const common::Address& addr, int32_t socket, const ConnectionID& original_dcid, const ConnectionID& original_scid,
    uint32_t version) {
    if (!retry_token_manager_) {
        common::LOG_ERROR("Retry token manager not initialized");
        return false;
    }

    // Generate a new server connection ID for this Retry
    uint8_t new_scid_data[kRetryCidLength];
    ConnectionIDGenerator::Instance().Generator(new_scid_data, kRetryCidLength);
    ConnectionID new_scid(new_scid_data, kRetryCidLength);

    // Generate Retry token
    std::string token = retry_token_manager_->GenerateToken(addr, original_dcid);
    if (token.empty()) {
        common::LOG_ERROR("Failed to generate Retry token");
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
        common::LOG_ERROR("Failed to allocate token buffer");
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
        common::LOG_ERROR("Failed to encode Retry packet for integrity tag");
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
        common::LOG_ERROR("Failed to compute Retry integrity tag");
        return false;
    }
    
    // Step 3: Set the computed tag and re-encode
    retry_packet.SetRetryIntegrityTag(integrity_tag);

    // Encode and send
    std::shared_ptr<NetPacket> net_packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    auto buffer = net_packet->GetData();

    if (!retry_packet.Encode(buffer)) {
        common::LOG_ERROR("Failed to encode Retry packet");
        return false;
    }

    net_packet->SetAddress(addr);
    net_packet->SetSocket(socket);
    sender_->Send(net_packet);

    common::LOG_INFO("Sent Retry packet. new_scid:%llu, token_len:%zu", new_scid.Hash(), token.size());
    return true;
}

bool ServerWorker::ValidateRetryToken(
    const std::string& token, const common::Address& addr, ConnectionID& out_original_dcid) {
    if (!retry_token_manager_) {
        return false;
    }
    return retry_token_manager_->ValidateToken(token, addr, out_original_dcid, retry_token_lifetime_);
}

void ServerWorker::SendVersionNegotiatePacket(const common::Address& addr, int32_t socket) {
    VersionNegotiationPacket version_negotiation_packet;
    for (auto version : kQuicVersions) {
        version_negotiation_packet.AddSupportVersion(version);
    }

    std::shared_ptr<NetPacket> net_packet = GlobalResource::Instance().GetThreadLocalPacketAllotor()->Malloc();
    auto buffer = net_packet->GetData();
    version_negotiation_packet.Encode(buffer);

    net_packet->SetAddress(addr);
    net_packet->SetSocket(socket);
    sender_->Send(net_packet);
    common::LOG_DEBUG("send version negotiate packet. packet size:%d", buffer->GetDataLength());
}

}  // namespace quic
}  // namespace quicx