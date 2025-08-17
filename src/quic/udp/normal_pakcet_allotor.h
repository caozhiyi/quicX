#ifndef QUIC_UDP_NORMAL_PACKET_ALLOTOR
#define QUIC_UDP_NORMAL_PACKET_ALLOTOR

#include "quic/udp/if_packet_allotor.h"

namespace quicx {
namespace quic {

/*
 normal packet allotor, alloc packet from normal memory
*/
class NormalPacketAllotor:
    public IPacketAllotor {
public:
    NormalPacketAllotor() {}
    virtual ~NormalPacketAllotor() {}

    std::shared_ptr<NetPacket> Malloc() override;
};

}
}

#endif
