
#ifndef QUIC_PACKET_VERSION_CONSULT_PACKET
#define QUIC_PACKET_VERSION_CONSULT_PACKET

#include <vector>
#include "long_header_packet.h"

namespace quicx {

class VersionNegotiationPacket: public LongHeaderPacket {
public:
    VersionNegotiationPacket();
    virtual ~VersionNegotiationPacket();

private:
    std::vector<uint32_t> _support_version;
};

}

#endif