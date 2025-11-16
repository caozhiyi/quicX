#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"
#include "quic/udp/pool_pakcet_allotor.h"

namespace quicx {
namespace quic {

PoolPacketAllotor::PoolPacketAllotor():
    packet_size_(200), // TODO: put into config
    pool_(common::MakeBlockMemoryPoolPtr(1500, 64)) {
    for (uint32_t i = 0; i < packet_size_; ++i) {
        auto chunk = std::make_shared<common::BufferChunk>(pool_);
        if (!chunk || !chunk->Valid()) {
            continue;
        }
        auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
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
        if (auto buffer = pkt->GetData()) {
            buffer->Clear();
        }
        packet_queue_.Push(pkt);
        return;
    }
    delete pkt;
}

}
}

