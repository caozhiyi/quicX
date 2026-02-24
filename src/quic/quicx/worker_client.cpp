#include "common/log/log.h"
#include "common/log/log_context.h"

#include "quic/connection/connection_client.h"
#include "quic/connection/error.h"
#include "quic/quicx/worker_client.h"

namespace quicx {
namespace quic {

// a normal worker
ClientWorker::ClientWorker(const QuicConfig& config, std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
    const QuicTransportParams& params, connection_state_callback connection_handler,
    std::shared_ptr<common::IEventLoop> event_loop):
    Worker(config, ctx, sender, params, connection_handler, event_loop) {}

ClientWorker::~ClientWorker() {}

void ClientWorker::Connect(const std::string& ip, uint16_t port, const std::string& alpn, int32_t timeout_ms,
    const std::string& resumption_session_der, const std::string& server_name) {
    auto conn = std::make_shared<ClientConnection>(ctx_, event_loop_,
        std::bind(&ClientWorker::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ClientWorker::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3));

    // Inject Sender for direct packet transmission
    conn->SetSender(sender_);

    // Set QUIC version from configuration
    conn->SetVersion(quic_version_);

    // RFC 9001: Enable Key Update if configured
    if (enable_key_update_) {
        conn->SetKeyUpdateEnabled(true);
        common::LOG_INFO("Key Update enabled for connection");
    }

    // RFC 9000 Section 6: Version Negotiation
    // Set callback to handle version negotiation from server
    conn->SetVersionNegotiationCallback(std::bind(&ClientWorker::HandleVersionNegotiation, this, conn, ip, port, alpn,
        timeout_ms, resumption_session_der, server_name, std::placeholders::_1));

    connecting_set_.insert(conn);

    if (resumption_session_der.empty()) {
        conn->Dial(common::Address(ip, port), alpn, params_, server_name);
    } else {
        conn->Dial(common::Address(ip, port), alpn, resumption_session_der, params_, server_name);
    }

    // Only set timeout for handshake phase (0 means no timeout - rely on idle timeout)
    // Skip if connection was already promoted by early connection callback during Dial()
    if (timeout_ms > 0 && connecting_set_.find(conn) != connecting_set_.end()) {
        auto timer_id = event_loop_->AddTimer(
            [conn, timeout_ms, this]() {
                // Only timeout if still in connecting state (handshake not completed)
                if (connecting_set_.find(conn) != connecting_set_.end()) {
                    common::LOG_WARN("handshake timeout for connection. cid:%llu, timeout_ms:%d",
                        conn->GetConnectionIDHash(), timeout_ms);
                    HandleConnectionTimeout(conn);
                }
            },
            timeout_ms);

        // Store timer ID so we can cancel it when handshake completes
        handshake_timers_[conn] = timer_id;
    }
}

bool ClientWorker::InnerHandlePacket(PacketParseResult& packet_info) {
    common::LOG_DEBUG("get packet. peer addr:%s", packet_info.net_packet_->GetAddress().AsString().c_str());

    // dispatch packet
    auto cid_code = packet_info.cid_.Hash();
    common::LOG_DEBUG("get packet. dcid:%llu", cid_code);
    auto conn = conn_map_.find(cid_code);
    if (conn != conn_map_.end()) {
        common::LogTagGuard guard("conn:" + std::to_string(cid_code));
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

    common::LOG_ERROR("get a packet with unknown connection id. id:%llu", cid_code);
    return false;
}

void ClientWorker::HandleHandshakeDone(std::shared_ptr<IConnection> conn) {
    // Cancel handshake timeout timer if it exists
    auto timer_it = handshake_timers_.find(conn);
    if (timer_it != handshake_timers_.end()) {
        event_loop_->RemoveTimer(timer_it->second);
        handshake_timers_.erase(timer_it);
        common::LOG_DEBUG(
            "handshake completed, cancelled timeout timer for connection. cid:%llu", conn->GetConnectionIDHash());
    }

    // Call base class implementation
    Worker::HandleHandshakeDone(conn);
}

void ClientWorker::HandleConnectionTimeout(std::shared_ptr<IConnection> conn) {
    // Clean up timer from map
    handshake_timers_.erase(conn);

    // Remove from connecting set and close connection
    if (connecting_set_.find(conn) != connecting_set_.end()) {
        connecting_set_.erase(conn);
        connection_handler_(conn, ConnectionOperation::kConnectionClose, QuicErrorCode::kConnectionTimeout,
            GetErrorString(QuicErrorCode::kConnectionTimeout));
    }
}

void ClientWorker::HandleVersionNegotiation(std::shared_ptr<IConnection> conn, const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms, const std::string& resumption_session_der,
    const std::string& server_name, uint32_t negotiated_version) {
    common::LOG_INFO("Version negotiation: server selected version 0x%08x, will reconnect", negotiated_version);

    // Clean up the current connection
    auto cid_hash = conn->GetConnectionIDHash();
    connecting_set_.erase(conn);
    conn_map_.erase(cid_hash);

    // Cancel handshake timer if exists
    auto timer_it = handshake_timers_.find(conn);
    if (timer_it != handshake_timers_.end()) {
        event_loop_->RemoveTimer(timer_it->second);
        handshake_timers_.erase(timer_it);
    }

    // Close the old connection
    conn->Close();

    // Schedule reconnection with negotiated version
    common::LOG_INFO("Reconnecting with negotiated version 0x%08x...", negotiated_version);

    // Create new connection with negotiated version
    auto new_conn = std::make_shared<ClientConnection>(ctx_, event_loop_,
        std::bind(&ClientWorker::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ClientWorker::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3));

    // CRITICAL: Set the negotiated version BEFORE dialing
    new_conn->SetVersion(negotiated_version);

    // Mark that version negotiation has been performed for this connection
    // This prevents infinite loops if the server sends another VN packet
    new_conn->SetVersionNegotiationDone();

    // Inject Sender
    new_conn->SetSender(sender_);

    // Enable Key Update if configured
    if (enable_key_update_) {
        new_conn->SetKeyUpdateEnabled(true);
    }

    // Set version negotiation callback for the new connection
    // If server sends another VN packet, the connection will be closed (see BaseConnection::OnVersionNegotiationPacket)
    new_conn->SetVersionNegotiationCallback(std::bind(&ClientWorker::HandleVersionNegotiation, this, new_conn, ip, port,
        alpn, timeout_ms, resumption_session_der, server_name, std::placeholders::_1));

    connecting_set_.insert(new_conn);

    // Dial with negotiated version
    if (resumption_session_der.empty()) {
        new_conn->Dial(common::Address(ip, port), alpn, params_, server_name);
    } else {
        new_conn->Dial(common::Address(ip, port), alpn, resumption_session_der, params_, server_name);
    }

    // Set timeout for new connection
    if (timeout_ms > 0) {
        auto timer_id = event_loop_->AddTimer(
            [new_conn, timeout_ms, this]() {
                if (connecting_set_.find(new_conn) != connecting_set_.end()) {
                    common::LOG_WARN("handshake timeout for reconnected connection. cid:%llu, timeout_ms:%d",
                        new_conn->GetConnectionIDHash(), timeout_ms);
                    HandleConnectionTimeout(new_conn);
                }
            },
            timeout_ms);
        handshake_timers_[new_conn] = timer_id;
    }
}

}  // namespace quic
}  // namespace quicx