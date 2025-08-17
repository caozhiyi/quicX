#ifndef QUIC_UDP_PACKET_ALLOTOR
#define QUIC_UDP_PACKET_ALLOTOR

#include <memory>
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

/*
 packet allotor interface, alloc packet
*/
class IPacketAllotor {
public:
    IPacketAllotor() {}
    virtual ~IPacketAllotor() {}

    virtual std::shared_ptr<NetPacket> Malloc() = 0;

    enum class PacketAllotorType {
        NORMAL,
        POOL
    };
    static std::shared_ptr<IPacketAllotor> MakePacketAllotor(PacketAllotorType type);
};

}
}

#endif
