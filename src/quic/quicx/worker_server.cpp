#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/quicx/worker_server.h"
#include "quic/connection/connection_server.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {
namespace quic {

ServerWorker::ServerWorker(std::shared_ptr<TLSCtx> ctx,
        const QuicTransportParams& params,
        connection_state_callback connection_handler):
    Worker(ctx, params, connection_handler) {
}

ServerWorker::~ServerWorker() {
}

bool ServerWorker::InnerHandlePacket(PacketInfo& packet_info) {
    // dispatch packet
    common::LOG_DEBUG("get packet. dcid:%llu", packet_info.cid_.Hash());
    auto conn = conn_map_.find(packet_info.cid_.Hash());
    if (conn != conn_map_.end()) {
        conn->second->OnPackets(packet_info.recv_time_, packet_info.packets_);
        return true;
    }

    // check init packet
    if (!InitPacketCheck(packet_info.packets_[0])) {
        common::LOG_ERROR("init packet check failed");
        SendVersionNegotiatePacket(packet_info.addr_);
        return false;
    }

    // create new connection
    auto new_conn = std::make_shared<ServerConnection>(ctx_, server_alpn_, time_,
        std::bind(&ServerWorker::HandleActiveSendConnection, this, std::placeholders::_1),
        std::bind(&ServerWorker::HandleHandshakeDone, this, std::placeholders::_1),
        std::bind(&ServerWorker::HandleAddConnectionId, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&ServerWorker::HandleRetireConnectionId, this, std::placeholders::_1),
        std::bind(&ServerWorker::HandleConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    new_conn->AddTransportParam(params_);
    connecting_set_.insert(new_conn);

    new_conn->AddRemoteConnectionId(packet_info.cid_);
    new_conn->SetPeerAddress(packet_info.addr_);
    new_conn->OnPackets(packet_info.recv_time_, packet_info.packets_);

    common::TimerTask task([new_conn, this]() {
        if (connecting_set_.find(new_conn) != connecting_set_.end()) {
            connecting_set_.erase(new_conn);
            common::LOG_DEBUG("connection timeout. cid:%llu", new_conn->GetConnectionIDHash());
        }
    });
    time_->AddTimer(task, 1000); // TODO add timeout to config
    return true;
}

void ServerWorker::SendVersionNegotiatePacket(common::Address& addr) {
    VersionNegotiationPacket version_negotiation_packet;
    for (auto version : kQuicVersions) {
        version_negotiation_packet.AddSupportVersion(version);
    }

    uint8_t buf[1500] = {0};
    auto buffer = std::make_shared<common::Buffer>(buf, sizeof(buf));
    version_negotiation_packet.Encode(buffer);

    auto net_packet = std::make_shared<NetPacket>();
    net_packet->SetData(buffer);
    net_packet->SetAddress(addr);
    sender_->Send(net_packet);
    common::LOG_DEBUG("send version negotiate packet. packet size:%d", buffer->GetDataLength());
}

}
}