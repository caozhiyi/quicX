#ifndef QUIC_QUICX_MSG_PARSER
#define QUIC_QUICX_MSG_PARSER

#include <vector>
#include <memory>

#include "quic/udp/net_packet.h"
#include "quic/packet/if_packet.h"
#include "quic/connection/connection_id.h"

namespace quicx {
namespace quic {

struct PacketParseResult {
    ConnectionID cid_;
    std::shared_ptr<NetPacket> net_packet_;
    std::vector<std::shared_ptr<IPacket>> packets_;

    PacketParseResult() = default;
    PacketParseResult(const PacketParseResult& other);
    PacketParseResult& operator=(const PacketParseResult& other);
};

// Msg parser
class MsgParser {
public:
    // Parse packets
    static bool ParsePacket(std::shared_ptr<NetPacket>& net_packet, PacketParseResult& packet_info);
};

}
}

#endif