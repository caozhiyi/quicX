#include "common/log/log.h"
#include "quic/common/version.h"
#include "quic/packet/init_packet.h"
#include "quic/packet/packet_decode.h"
#include "quic/process/server_processor.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {

ServerProcessor::ServerProcessor() {

}

ServerProcessor::~ServerProcessor() {

}

bool ServerProcessor::HandlePacket(std::shared_ptr<IUdpPacket> udp_packet) {
    LOG_INFO("get packet from %s", udp_packet->GetPeerAddress().AsString().c_str());

    std::vector<std::shared_ptr<IPacket>> packets;
    if(!DecodePackets(udp_packet->GetBuffer(), packets)) {
        // todo send version negotiate packet
        return false;
    }

    uint8_t* cid = nullptr;
    uint16_t len = 0;
    if (!GetDestConnectionId(packets, cid, len)) {
        LOG_ERROR("get dest connection id failed.");
        return false;
    }
    
    // dispatch packet
    long cid_code = ConnectionIDGenerator::Instance().Hash(cid, len);
    auto conn = _conn_map.find(cid_code);
    if (conn != _conn_map.end()) {
        conn->second->OnPackets(packets);
        return true;
    }

    // check init packet?
    if (!InitPacketCheck(packets[0])) {
        return false;
    }   

    auto new_conn = std::make_shared<ServerConnection>(_ctx);
    new_conn->AddConnectionId(cid, len);
    _conn_map[cid_code] = new_conn;
    new_conn->OnPackets(packets);

    return true;
}

bool ServerProcessor::HandlePackets(const std::vector<std::shared_ptr<IUdpPacket>>& udp_packets) {
    for (size_t i = 0; i < udp_packets.size(); i++) {
        HandlePacket(udp_packets[i]);
    }
    return true;
}

bool InitPacketCheck(std::shared_ptr<IPacket> packet) {
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