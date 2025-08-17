#include "common/buffer/buffer.h"
#include "quic/udp/normal_pakcet_allotor.h"

namespace quicx {
namespace quic {

std::shared_ptr<NetPacket> NormalPacketAllotor::Malloc() {
    uint8_t* mem = new uint8_t[1500];
    auto buffer = std::shared_ptr<common::IBuffer>(
        new common::Buffer(mem, 1500),
        [mem](common::IBuffer* p){ delete[] mem; delete p; }
    );
    std::shared_ptr<NetPacket> pkt = std::make_shared<NetPacket>();
    pkt->SetData(buffer);
    return pkt;
}

}
}

