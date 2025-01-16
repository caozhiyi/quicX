#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/quicx/processor_server.h"
#include "quic/connection/server_connection.h"
#include "quic/packet/version_negotiation_packet.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

ProcessorServer::ProcessorServer(std::shared_ptr<TLSCtx> ctx,
    connection_state_callback connection_handler):
    ProcessorBase(ctx, connection_handler) {
}

ProcessorServer::~ProcessorServer() {
}

void ProcessorServer::SetServerAlpn(const std::string& alpn) {
    server_alpn_ = alpn;
}

bool ProcessorServer::HandlePacket(std::shared_ptr<INetPacket> packet) {
    common::LOG_INFO("get packet from %s", packet->GetAddress().AsString().c_str());

    uint8_t* cid;
    uint16_t len = 0;
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodeNetPakcet(packet, packets, cid, len)) {
        common::LOG_ERROR("decode packet failed");
        SendVersionNegotiatePacket(packet);
        return false;
    }
    
    // dispatch packet
    auto cid_code = ConnectionIDGenerator::Instance().Hash(cid, len);
    auto conn = conn_map_.find(cid_code);
    if (conn != conn_map_.end()) {
        conn->second->OnPackets(packet->GetTime(), packets);
        return true;
    }

    // if pakcet is a short header packet, but we can't find in connection map, the connection may exist in other thread.
    // that may happen when ip of client changed.
    auto pkt_type = packets[0]->GetHeader()->GetPacketType();
    if (pkt_type == PacketType::PT_1RTT && connection_transfor_) {
        connection_transfor_->TryCatchConnection(cid_code);
        return true;
    }

    // check init packet
    if (!InitPacketCheck(packets[0])) {
        common::LOG_ERROR("init packet check failed");
        SendVersionNegotiatePacket(packet);
        return false;
    }

    // create new connection
    auto new_conn = std::make_shared<ServerConnection>(ctx_, server_alpn_, time_,
        std::bind(&ProcessorServer::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&ProcessorServer::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&ProcessorServer::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ProcessorServer::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&ProcessorServer::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    connecting_map_[cid_code] = new_conn;
    // TODO add timer to check connection status

    new_conn->SetPeerAddress(packet->GetAddress());
    new_conn->AddRemoteConnectionId(cid, len);
    new_conn->OnPackets(packet->GetTime(), packets);
    return true;
}

void ProcessorServer::SendVersionNegotiatePacket(std::shared_ptr<INetPacket> packet) {
    VersionNegotiationPacket version_negotiation_packet;
    for (auto version : __quic_versions) {
        version_negotiation_packet.AddSupportVersion(version);
    }

    uint8_t buf[1500] = {0};
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    version_negotiation_packet.Encode(buffer);

    auto net_packet = std::make_shared<INetPacket>();
    net_packet->SetData(buffer);
    net_packet->SetAddress(packet->GetAddress());
    net_packet->SetSocket(packet->GetSocket());
    sender_->Send(net_packet);
}

}
}