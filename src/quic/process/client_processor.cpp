#include "common/log/log.h"
#include "quic/process/client_processor.h"

namespace quicx {

ClientProcessor::ClientProcessor() {
    
}

ClientProcessor::~ClientProcessor() {

}

bool ClientProcessor::HandlePacket(std::shared_ptr<UdpPacketIn> udp_packet) {
    LOG_INFO("get packet from %s", udp_packet->GetPeerAddress().AsString().c_str());

    // dispatch packet
    auto packets = udp_packet->GetPackets();
    uint64_t cid_code = udp_packet->GetConnectionHashCode();
    auto conn = _conn_map.find(cid_code);
    if (conn != _conn_map.end()) {
        conn->second->OnPackets(packets);
        return true;
    }

    return false;
}

bool ClientProcessor::HandlePackets(const std::vector<std::shared_ptr<UdpPacketIn>>& udp_packets) {
    for (size_t i = 0; i < udp_packets.size(); i++) {
        HandlePacket(udp_packets[i]);
    }
    return true;
}

}