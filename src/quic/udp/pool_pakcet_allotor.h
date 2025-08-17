#ifndef QUIC_UDP_POOL_PACKET_ALLOTOR
#define QUIC_UDP_POOL_PACKET_ALLOTOR

#include "quic/udp/normal_pakcet_allotor.h"
#include "common/structure/thread_safe_queue.h"

namespace quicx {
namespace quic {

/*
 pool packet allotor, alloc packet from pool memory
*/
class PoolPacketAllotor:
    public NormalPacketAllotor {
public:
    PoolPacketAllotor();
    virtual ~PoolPacketAllotor();

    std::shared_ptr<NetPacket> Malloc() override;

private:
    void Free(NetPacket* pkt);

    private:
    uint32_t packet_size_;
    common::ThreadSafeQueue<NetPacket*> packet_queue_;
};

}
}

#endif
