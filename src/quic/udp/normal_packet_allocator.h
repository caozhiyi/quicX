#ifndef QUIC_UDP_NORMAL_PACKET_ALLOCATOR
#define QUIC_UDP_NORMAL_PACKET_ALLOCATOR

#include "quic/udp/if_packet_allocator.h"

namespace quicx {
namespace quic {

/*
 normal packet allocator, alloc packet from normal memory
*/
class NormalPacketAllocator: public IPacketAllocator {
public:
    NormalPacketAllocator() {}
    virtual ~NormalPacketAllocator() {}

    std::shared_ptr<NetPacket> Malloc() override;
};

}  // namespace quic
}  // namespace quicx

#endif
