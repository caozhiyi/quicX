
#ifndef QUIC_PACKET_VERSION_NEGOTIATION_PACKET
#define QUIC_PACKET_VERSION_NEGOTIATION_PACKET

#include <vector>
#include "long_header.h"

namespace quicx {

class VersionNegotiationPacket: public LongHeader {
public:
    VersionNegotiationPacket();
    virtual ~VersionNegotiationPacket();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<Frame> frame);

private:
    std::vector<uint32_t> _support_version;
};

}

#endif