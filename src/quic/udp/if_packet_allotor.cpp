#include "quic/udp/if_packet_allotor.h"
#include "quic/udp/pool_pakcet_allotor.h"
#include "quic/udp/normal_pakcet_allotor.h"

namespace quicx {
namespace quic {

std::shared_ptr<IPacketAllotor> IPacketAllotor::MakePacketAllotor(IPacketAllotor::PacketAllotorType type) {
    if (type == IPacketAllotor::PacketAllotorType::NORMAL) {
        return std::make_shared<NormalPacketAllotor>();

    } else if (type == IPacketAllotor::PacketAllotorType::POOL) {
        return std::make_shared<PoolPacketAllotor>();
    }
    return nullptr;
}

}
}
