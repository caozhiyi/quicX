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
    return common::MakeBlockMemoryPoolPtr(
        1500, 4);  // Increased from 1024 to 1500 for RFC9000 compliance (min 1200 bytes)
}

std::shared_ptr<IPacketAllotor> GlobalResource::MakeDefaultPacketAllotor() {
    return IPacketAllotor::MakePacketAllotor(IPacketAllotor::PacketAllotorType::POOL);
}

}  // namespace quic
}  // namespace quicx
