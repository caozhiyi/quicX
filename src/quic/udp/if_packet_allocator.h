#ifndef QUIC_UDP_PACKET_ALLOCATOR
#define QUIC_UDP_PACKET_ALLOCATOR


#include <memory>

#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

/*
 packet allocator interface, alloc packet
*/
class IPacketAllocator {
public:
    IPacketAllocator() {}
    virtual ~IPacketAllocator() {}

    virtual std::shared_ptr<NetPacket> Malloc() = 0;

    enum class PacketAllocatorType { NORMAL, POOL };
    static std::shared_ptr<IPacketAllocator> MakePacketAllocator(PacketAllocatorType type);
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_UDP_PACKET_ALLOCATOR
