#ifndef QUIC_QUICX_GLOBAL_RESOURCE
#define QUIC_QUICX_GLOBAL_RESOURCE

#include <memory>

#include "common/alloter/pool_block.h"
#include "common/util/singleton.h"
#include "quic/udp/if_packet_allotor.h"

namespace quicx {
namespace quic {

class GlobalResource:
    public common::Singleton<GlobalResource> {
public:
    std::shared_ptr<common::BlockMemoryPool> GetThreadLocalBlockPool();
    std::shared_ptr<quic::IPacketAllotor> GetThreadLocalPacketAllotor();

private:
    friend class common::Singleton<GlobalResource>;
    GlobalResource();
    ~GlobalResource() = default;

    std::shared_ptr<common::BlockMemoryPool> MakeDefaultPool();
    std::shared_ptr<IPacketAllotor> MakeDefaultPacketAllotor();
};

}
}

#endif

