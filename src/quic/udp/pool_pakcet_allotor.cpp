#include "common/buffer/buffer.h"
#include "quic/udp/pool_pakcet_allotor.h"

namespace quicx {
namespace quic {

PoolPacketAllotor::PoolPacketAllotor():
    packet_size_(200) {
    for (uint32_t i = 0; i < packet_size_; ++i) {
        uint8_t* mem = new uint8_t[1500];
        auto buffer = std::shared_ptr<common::IBuffer>(
            new common::Buffer(mem, 1500),
            [mem](common::IBuffer* p){ delete[] mem; delete p; }
        );
        auto pkt = new NetPacket();
        pkt->SetData(buffer);
        packet_queue_.Push(pkt);
    }
}

PoolPacketAllotor::~PoolPacketAllotor() {
    NetPacket* pkt;
    while (!packet_queue_.Empty()) {
        if (packet_queue_.Pop(pkt)) {
            delete pkt;
        }
    }
}

std::shared_ptr<NetPacket> PoolPacketAllotor::Malloc() {
    NetPacket* pkt;
    if (packet_queue_.Pop(pkt)) {
        std::shared_ptr<NetPacket> ret_pkt(pkt, [this](NetPacket* pkt) { Free(pkt); });
        return ret_pkt;
    }
    return NormalPacketAllotor::Malloc();
}

void PoolPacketAllotor::Free(NetPacket* pkt) {
    if (packet_queue_.Size() < packet_size_) {
        packet_queue_.Push(pkt);
    }
}

}
}

