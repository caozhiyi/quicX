#include "common/log/log.h"
#include "quic/connection/error.h"
#include "quic/quicx/worker_client.h"
#include "quic/connection/connection_client.h"

namespace quicx {
namespace quic {

// a normal worker
ClientWorker::ClientWorker(const QuicConfig& config, std::shared_ptr<TLSCtx> ctx, std::shared_ptr<ISender> sender,
    const QuicTransportParams& params, connection_state_callback connection_handler, std::shared_ptr<common::IEventLoop> event_loop):
    Worker(config, ctx, sender, params, connection_handler, event_loop) {}

ClientWorker::~ClientWorker() {}

void ClientWorker::Connect(const std::string& ip, uint16_t port, const std::string& alpn, int32_t timeout_ms,
    const std::string& resumption_session_der) {
    auto conn = std::make_shared<ClientConnection>(ctx_, event_loop_,
        std::bind(&ClientWorker::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ClientWorker::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&ClientWorker::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3));

    connecting_set_.insert(conn);
    if (resumption_session_der.empty()) {
        conn->Dial(common::Address(ip, port), alpn, params_);
    } else {
        conn->Dial(common::Address(ip, port), alpn, resumption_session_der, params_);
    }

    event_loop_->AddTimer([conn, this]() { HandleConnectionTimeout(conn); }, timeout_ms);
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

void ClientWorker::HandleConnectionTimeout(std::shared_ptr<IConnection> conn) {
    if (conn_map_.find(conn->GetConnectionIDHash()) != conn_map_.end()) {
        conn_map_.erase(conn->GetConnectionIDHash());
        connection_handler_(conn, ConnectionOperation::kConnectionClose, QuicErrorCode::kConnectionTimeout,
            GetErrorString(QuicErrorCode::kConnectionTimeout));
    }
}

}  // namespace quic
}  // namespace quicx