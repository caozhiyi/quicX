#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {

thread_local std::shared_ptr<common::BlockMemoryPool> pool_;
thread_local std::shared_ptr<quic::IPacketAllotor> packet_allotor_;
thread_local std::weak_ptr<common::IEventLoop> thread_event_loop_;

GlobalResource::GlobalResource() {}

std::shared_ptr<common::BlockMemoryPool> GlobalResource::GetThreadLocalBlockPool() {
    if (!pool_) {
        pool_ = MakeDefaultPool();
        // Set event loop if already registered for this thread
        auto loop = thread_event_loop_.lock();
        if (loop) {
            pool_->SetEventLoop(loop);
        }
    }
    return pool_;
}

std::shared_ptr<quic::IPacketAllotor> GlobalResource::GetThreadLocalPacketAllotor() {
    if (!packet_allotor_) {
        packet_allotor_ = MakeDefaultPacketAllotor();
    }
    return packet_allotor_;
}

void GlobalResource::RegisterThreadEventLoop(std::shared_ptr<common::IEventLoop> event_loop) {
    thread_event_loop_ = event_loop;
    // Also set it on the pool if already created
    if (pool_) {
        pool_->SetEventLoop(event_loop);
    }
}

std::weak_ptr<common::IEventLoop> GlobalResource::GetThreadEventLoop() {
    return thread_event_loop_;
}

std::shared_ptr<common::BlockMemoryPool> GlobalResource::MakeDefaultPool() {
    // 1500B aligns with typical Ethernet MTU and is required by the
    // outbound packet build path: BaseConnection::TrySend allocates a
    // BufferChunk from this same pool to serialize the entire encrypted
    // QUIC packet (header + STREAM frame payload up to 1300B + AEAD tag).
    // Shrinking this below ~1350B causes BuildDataPacket to fail once
    // STREAM payload approaches the 1300B cap. Also satisfies RFC9000
    // minimum datagram size of 1200B.
    //
    // NOTE: The "chunk size 1500 vs STREAM send_size cap 1300" mismatch
    // observed via PerfProbe is real (causes 1300+200 fragmentation in
    // send_buffer), but the fix must NOT shrink this shared pool.
    // Future fix should either (a) use a dedicated smaller pool for
    // stream send_buffer, or (b) raise the STREAM send_size cap to
    // match chunk size.
    return common::MakeBlockMemoryPoolPtr(1500, 4);
}

std::shared_ptr<IPacketAllotor> GlobalResource::MakeDefaultPacketAllotor() {
    return IPacketAllotor::MakePacketAllotor(IPacketAllotor::PacketAllotorType::POOL);
}

}  // namespace quic
}  // namespace quicx
