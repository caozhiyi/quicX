#include "quic/quicx/global_resource.h"

namespace quicx {
namespace quic {

thread_local std::shared_ptr<common::BlockMemoryPool> pool_;
thread_local std::shared_ptr<quic::IPacketAllotor> packet_allotor_;

std::shared_ptr<common::BlockMemoryPool> GlobalResource::GetThreadLocalBlockPool() {
    if (!pool_) {
        pool_ = MakeDefaultPool();
    }
    return pool_;
}

std::shared_ptr<quic::IPacketAllotor> GlobalResource::GetThreadLocalPacketAllotor() {
    if (!packet_allotor_) {
        packet_allotor_ = MakeDefaultPacketAllotor();
    }
    return packet_allotor_;
}

GlobalResource::GlobalResource() {
    pool_ = MakeDefaultPool();
    packet_allotor_ = MakeDefaultPacketAllotor();
}

std::shared_ptr<common::BlockMemoryPool> GlobalResource::MakeDefaultPool() {
    return common::MakeBlockMemoryPoolPtr(1024, 4);
}

std::shared_ptr<IPacketAllotor> GlobalResource::MakeDefaultPacketAllotor() {
    return IPacketAllotor::MakePacketAllotor(IPacketAllotor::PacketAllotorType::POOL);
}

}
}

