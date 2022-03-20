
#ifndef QUIC_PACKET_VERSION_NEGOTIATION_PACKET
#define QUIC_PACKET_VERSION_NEGOTIATION_PACKET

#include <vector>
#include "quic/packet/long_header.h"

namespace quicx {

class VersionNegotiationPacket: public LongHeader {
public:
    VersionNegotiationPacket();
    virtual ~VersionNegotiationPacket();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

private:
    std::vector<uint32_t> _support_version;
};

}

#endif