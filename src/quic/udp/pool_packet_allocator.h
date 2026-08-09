#ifndef QUIC_UDP_POOL_PACKET_ALLOCATOR
#define QUIC_UDP_POOL_PACKET_ALLOCATOR

#include <memory>

#include "common/allocator/pool_block.h"
#include "common/structure/thread_safe_queue.h"
#include "quic/udp/normal_packet_allocator.h"

namespace quicx {
namespace quic {

/*
 pool packet allocator, alloc packet from pool memory
*/
class PoolPacketAllocator: public NormalPacketAllocator {
public:
    PoolPacketAllocator();
    virtual ~PoolPacketAllocator();

    std::shared_ptr<NetPacket> Malloc() override;

private:
    void Free(NetPacket* pkt);

    uint32_t packet_size_;
    std::shared_ptr<common::BlockMemoryPool> pool_;
    common::ThreadSafeQueue<NetPacket*> packet_queue_;
};

}  // namespace quic
}  // namespace quicx

#endif
