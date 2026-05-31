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
    uint32_t datagram_size_ = 0;  // Original UDP datagram size before DecodePackets consumes the buffer

    // Rule of Zero: rely on compiler-generated copy/move/dtor.
    // NOTE: previously this struct declared a user-defined copy ctor / copy assign
    // (with bodies that were byte-for-byte equivalent to the defaults). Per Rule of
    // Five, that *suppressed* the implicit move ctor / move assign, which caused
    // std::move(packet_info) inside ThreadSafeBlockQueue::Emplace to silently
    // degrade into a deep copy. Each "phantom copy" leaked refcount on
    // net_packet_ and every shared_ptr in packets_, pinning NetPacket and its
    // BufferChunk forever. Confirmed by heaptrack: ~92MB out of 132MB total
    // leak traced to PacketParseResult copy ctor inside deque::emplace_back.
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