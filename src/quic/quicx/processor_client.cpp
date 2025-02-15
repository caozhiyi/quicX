#include "common/log/log.h"
#include "quic/connection/error.h"
#include "quic/quicx/processor_client.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

ProcessorClient::ProcessorClient(std::shared_ptr<TLSCtx> ctx,
    const QuicTransportParams& params,
    connection_state_callback connection_handler):
    ProcessorBase(ctx, params, connection_handler) {

}

ProcessorClient::~ProcessorClient() {

}

void ProcessorClient::Connect(const std::string& ip, uint16_t port,
    const std::string& alpn, int32_t timeout_ms) {
    auto conn = std::make_shared<ClientConnection>(ctx_, time_,
        std::bind(&ProcessorClient::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&ProcessorClient::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&ProcessorClient::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ProcessorClient::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&ProcessorClient::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    connecting_set_.insert(conn);
    conn->Dial(common::Address(ip, port), alpn, params_);

    common::TimerTask task([conn, this]() {
        HandleConnectionTimeout(conn);
    });
    time_->AddTimer(task, timeout_ms);
}

bool ProcessorClient::HandlePacket(std::shared_ptr<INetPacket> packet) {
    common::LOG_INFO("get packet. peer addr:%s, packet len:%d",
        packet->GetAddress().AsString().c_str(), packet->GetData()->GetDataLength());

    uint8_t* cid;
    uint16_t len = 0;
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodeNetPakcet(packet, packets, cid, len)) {
        common::LOG_ERROR("decode packet failed");
        return false;
    }
    
    // dispatch packet
    auto cid_code = ConnectionIDGenerator::Instance().Hash(cid, len);
    common::LOG_DEBUG("get packet. dcid:%llu", cid_code);
    auto conn = conn_map_.find(cid_code);
    if (conn != conn_map_.end()) {
        conn->second->OnPackets(packet->GetTime(), packets);
        return true;
    }

    if (packets[0]->GetHeader()->GetPacketType() == PacketType::kNegotiationPacketType) {
        common::LOG_DEBUG("get a negotiation packet"); // TODO handle negotiation packet
        return true;
    }

    // if pakcet is a short header packet, but we can't find in connection map, the connection may exist in other thread.
    // that may happen when ip of client changed.
    auto pkt_type = packets[0]->GetHeader()->GetPacketType();
    if (pkt_type == PacketType::k1RttPacketType && connection_transfor_) {
        connection_transfor_->TryCatchConnection(cid_code);
        return true;
    }

    common::LOG_ERROR("get a packet with unknown connection id");
    return false;
}

void ProcessorClient::HandleConnectionTimeout(std::shared_ptr<IConnection> conn) {
    if (conn_map_.find(conn->GetConnectionIDHash()) != conn_map_.end()) {
        conn_map_.erase(conn->GetConnectionIDHash());
        connection_handler_(conn, QuicErrorCode::kConnectionTimeout, GetErrorString(QuicErrorCode::kConnectionTimeout));
    }
}

}
}