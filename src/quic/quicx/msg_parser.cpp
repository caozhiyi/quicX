#include "common/log/log.h"
#include "quic/quicx/msg_parser.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/header/long_header.h"
#include "quic/packet/header/short_header.h"

namespace quicx {
namespace quic {

PacketParseResult::PacketParseResult(const PacketParseResult& other) {
    cid_ = other.cid_;
    packets_ = other.packets_;
    net_packet_ = other.net_packet_;
}

PacketParseResult& PacketParseResult::operator=(const PacketParseResult& other) {
    cid_ = other.cid_;
    packets_ = other.packets_;
    net_packet_ = other.net_packet_;
    return *this;
}

bool MsgParser::ParsePacket(std::shared_ptr<NetPacket>& net_packet, PacketParseResult& packet_info) {
    if(!DecodePackets(net_packet->GetData(), packet_info.packets_)) {
        common::LOG_ERROR("decode packet failed");
        return false;
    }

    if (packet_info.packets_.empty()) {
        common::LOG_ERROR("parse packet list is empty.");
        return false;
    }
    
    uint8_t* cid_buf = nullptr;
    uint16_t cid_len = 0;
    auto first_packet_header = packet_info.packets_[0]->GetHeader();
    if (first_packet_header->GetHeaderType() == PacketHeaderType::kShortHeader) {
        auto short_header = dynamic_cast<ShortHeader*>(first_packet_header);
        cid_len = short_header->GetDestinationConnectionIdLength();
        cid_buf = (uint8_t*)short_header->GetDestinationConnectionId();

    } else {
        auto long_header = dynamic_cast<LongHeader*>(first_packet_header);
        cid_len = long_header->GetDestinationConnectionIdLength();
        cid_buf = (uint8_t*)long_header->GetDestinationConnectionId();
    }

    packet_info.cid_.SetID(cid_buf, cid_len);
    packet_info.net_packet_ = net_packet;
    return true;
}

}
}