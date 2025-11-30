
#include "common/log/log.h"

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

    // Set immediate send callback for immediate ACK sending
    conn->SetImmediateSendCallback([this, conn](std::shared_ptr<common::IBuffer> buffer, const common::Address& addr) {
        SendImmediate(buffer, addr, conn->GetSocket());
    });

    connecting_set_.insert(conn);

    if (resumption_session_der.empty()) {
        conn->Dial(common::Address(ip, port), alpn, params_, server_name);
    } else {
        conn->Dial(common::Address(ip, port), alpn, resumption_session_der, params_, server_name);
    }

    // Only set timeout for handshake phase (0 means no timeout - rely on idle timeout)
    if (timeout_ms > 0) {
        auto timer_id = event_loop_->AddTimer([conn, timeout_ms, this]() {
            // Only timeout if still in connecting state (handshake not completed)
            if (connecting_set_.find(conn) != connecting_set_.end()) {
                common::LOG_WARN("handshake timeout for connection. cid:%llu, timeout_ms:%d",
                    conn->GetConnectionIDHash(), timeout_ms);
                HandleConnectionTimeout(conn);
            }
        }, timeout_ms);

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

    if (packet_info.packets_[0]->GetHeader()->GetPacketType() == PacketType::kNegotiationPacketType) {
        common::LOG_DEBUG("get a negotiation packet");  // TODO handle negotiation packet
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
        common::LOG_DEBUG("handshake completed, cancelled timeout timer for connection. cid:%llu",
            conn->GetConnectionIDHash());
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

}  // namespace quic
}  // namespace quicx