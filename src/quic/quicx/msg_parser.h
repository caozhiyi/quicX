#ifndef QUIC_QUICX_MSG_PARSER
#define QUIC_QUICX_MSG_PARSER

#include <vector>
#include <memory>

#include "quic/udp/net_packet.h"
#include "quic/packet/if_packet.h"
#include "quic/connection/connection_id.h"

namespace quicx {
namespace quic {

struct PacketInfo {
    uint64_t recv_time_;
    ConnectionID cid_;
    common::Address addr_;
    std::vector<std::shared_ptr<IPacket>> packets_;
    PacketInfo(): recv_time_(0) {}
};

// Msg parser
class MsgParser {
public:
    // Parse packets
    static bool ParsePacket(std::shared_ptr<NetPacket> net_packet, PacketInfo& packet_info);
};

}
}

#endif