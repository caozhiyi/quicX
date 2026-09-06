#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/log/log.h"

#include "quic/udp/normal_packet_allocator.h"

namespace quicx {
namespace quic {

std::shared_ptr<NetPacket> NormalPacketAllocator::Malloc() {
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(1500);
    if (!chunk || !chunk->Valid()) {
        LOG_ERROR("failed to allocate buffer chunk");
        return std::make_shared<NetPacket>();
    }

    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    auto pkt = std::make_shared<NetPacket>();
    pkt->SetData(buffer);
    return pkt;
}

}  // namespace quic
}  // namespace quicx
