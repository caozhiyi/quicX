#ifndef QUIC_PACKET_PACKET_DECODE
#define QUIC_PACKET_PACKET_DECODE

#include <memory>
#include <vector>

namespace quicx {
namespace quic {

class IPacket;
class IBufferRead;
bool DecodePackets(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IPacket>>& packets);

}
}


#endif
