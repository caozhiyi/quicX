#ifndef QUIC_PACKET_PACKET_DECODE
#define QUIC_PACKET_PACKET_DECODE

#include <memory>
#include <vector>

namespace quicx {

class IPacket;
class IBufferRead;
bool DecodePackets(std::shared_ptr<IBufferRead> buffer, std::shared_ptr<IPacket>& packets);

}


#endif
