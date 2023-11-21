#include "common/log/log.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/header/long_header.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {

UdpPacketIn::UdpPacketIn():
    _connection_hash_code(0) {

}

UdpPacketIn::~UdpPacketIn() {

}

bool UdpPacketIn::DecodePacket() {
    if(!DecodePackets(_buffer, _packets)) {
        // todo send version negotiate packet
        return false;
    }

    if (_packets.empty()) {
        common::LOG_ERROR("parse packet list is empty.");
        return false;
    }
    
    uint8_t* cid = nullptr;
    uint16_t len = 0;
    auto first_packet_header = _packets[0]->GetHeader();
    if (first_packet_header->GetHeaderType() == PHT_SHORT_HEADER) {
        // todo get short header dcid
    } else {
        auto long_header = dynamic_cast<LongHeader*>(first_packet_header);
        len = long_header->GetDestinationConnectionIdLength();
        cid = (uint8_t*)long_header->GetDestinationConnectionId();
    }

    _connection_hash_code = ConnectionIDGenerator::Instance().Hash(cid, len);
    return true;
}

void UdpPacketIn::GetConnection(uint8_t* id, uint16_t& len) {
    id = _connection_id;
    len = _connection_id_len;
}

}
}
