#include <thread>

#include "common/buffer/buffer_chunk.h"
#include "common/buffer/single_block_buffer.h"

#include "quic/config.h"
#include "quic/udp/pool_packet_allocator.h"

namespace quicx {
namespace quic {

PoolPacketAllocator::PoolPacketAllocator():
    packet_size_(kPacketPoolSize),
    pool_(common::MakeBlockMemoryPoolPtr(kPacketBufferSize, kPacketPoolBlockCount)) {
    // The pool is owned by the thread that creates this allocator. Mark it so that
    // blocks freed on a different thread (e.g. a worker releasing a received packet)
    // are deferred to the lock-free handback stack instead of corrupting the free list.
    pool_->SetOwnerThread(std::this_thread::get_id());
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

PoolPacketAllocator::~PoolPacketAllocator() {
    NetPacket* pkt;
    while (!packet_queue_.Empty()) {
        if (packet_queue_.Pop(pkt)) {
            delete pkt;
        }
    }
}

std::shared_ptr<NetPacket> PoolPacketAllocator::Malloc() {
    NetPacket* pkt;
    if (packet_queue_.Pop(pkt)) {
        // Capture a weak_ptr instead of raw `this`: the allocator is thread-local
        // and may be destroyed when its owning thread exits while in-flight
        // NetPackets (handed to another thread for processing) are still alive.
        // With raw `this` the deleter would touch a freed allocator (UAF); with
        // weak_ptr it safely falls back to `delete pkt` once the allocator is gone.
        std::weak_ptr<PoolPacketAllocator> weak_self = shared_from_this();
        std::shared_ptr<NetPacket> ret_pkt(pkt, [weak_self](NetPacket* pkt) {
            if (auto self = weak_self.lock()) {
                self->Free(pkt);
            } else {
                delete pkt;
            }
        });
        return ret_pkt;
    }
    return NormalPacketAllocator::Malloc();
}

void PoolPacketAllocator::Free(NetPacket* pkt) {
    if (packet_queue_.Size() < packet_size_) {
        if (auto buffer = pkt->GetData()) {
            buffer->Clear();
        }
        packet_queue_.Push(pkt);
        return;
    }
    delete pkt;
}

}  // namespace quic
}  // namespace quicx
