#include "quic/udp/if_packet_allocator.h"
#include "quic/udp/normal_packet_allocator.h"
#include "quic/udp/pool_packet_allocator.h"

namespace quicx {
namespace quic {

std::shared_ptr<IPacketAllocator> IPacketAllocator::MakePacketAllocator(IPacketAllocator::PacketAllocatorType type) {
    if (type == IPacketAllocator::PacketAllocatorType::NORMAL) {
        return std::make_shared<NormalPacketAllocator>();

    } else if (type == IPacketAllocator::PacketAllocatorType::POOL) {
        return std::make_shared<PoolPacketAllocator>();
    }
    return nullptr;
}

}  // namespace quic
}  // namespace quicx
