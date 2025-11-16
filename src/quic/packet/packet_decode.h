#ifndef QUIC_PACKET_PACKET_DECODE
#define QUIC_PACKET_PACKET_DECODE

#include <memory>
#include <vector>

#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

class IPacket;
class IBufferRead;
bool DecodePackets(std::shared_ptr<common::IBuffer> buffer, std::vector<std::shared_ptr<IPacket>>& packets);

}
}


#endif
