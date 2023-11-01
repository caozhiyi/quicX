#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/process/server_processor.h"
#include "quic/connection/server_connection.h"

namespace quicx {

ServerProcessor::ServerProcessor() {

}

ServerProcessor::~ServerProcessor() {

}

bool ServerProcessor::HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet, std::shared_ptr<ITimer> timer) {
    LOG_INFO("get packet from %s", udp_packet->GetPeerAddress().AsString().c_str());

    // dispatch packet
    auto packets = udp_packet->GetPackets();
    uint64_t cid_code = udp_packet->GetConnectionHashCode();
    auto conn = _conn_map.find(cid_code);
    if (conn != _conn_map.end()) {
        conn->second->OnPackets(packets);
        return true;
    }

    // check init packet?
    if (!InitPacketCheck(packets[0])) {
        return false;
    }

    uint8_t* cid;
    uint16_t len = 0;
    udp_packet->GetConnection(cid, len);

    auto new_conn = std::make_shared<ServerConnection>(_ctx);
    new_conn->AddConnectionId(cid, len);
    new_conn->SetAddConnectionIDCB(_add_connection_id_cb);
    new_conn->SetRetireConnectionIDCB(_retire_connection_id_cb);
    _conn_map[cid_code] = new_conn;
    new_conn->OnPackets(packets);

    return true;
}

bool ServerProcessor::HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets, std::shared_ptr<ITimer> timer) {
    for (size_t i = 0; i < udp_packets.size(); i++) {
        HandlePacket(udp_packets[i], timer);
    }
    return true;
}

bool ServerProcessor::InitPacketCheck(std::shared_ptr<IPacket> packet) {
    if (packet->GetHeader()->GetPacketType() != PT_INITIAL) {
        LOG_ERROR("recv packet whitout connection.");
        return false;
    }

    auto init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    uint32_t version = ((LongHeader*)init_packet->GetHeader())->GetVersion();
    if (!VersionCheck(version)) {
        // TODO may generate a version negotiation packet
        return false;
    }

    return true;
}

}