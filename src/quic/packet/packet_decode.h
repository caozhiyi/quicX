#ifndef QUIC_PACKET_PACKET_DECODE
#define QUIC_PACKET_PACKET_DECODE

#include <memory>
#include <vector>

namespace quicx {

class IPacket;
class IBufferReadOnly;
bool DecodePackets(std::shared_ptr<IBufferReadOnly> buffer, std::vector<std::shared_ptr<IPacket>>& packets);

}


#endif
