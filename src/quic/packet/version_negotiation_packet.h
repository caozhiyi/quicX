
#ifndef QUIC_PACKET_VERSION_NEGOTIATION_PACKET
#define QUIC_PACKET_VERSION_NEGOTIATION_PACKET

#include <memory>
#include <vector>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class VersionNegotiationPacket:
    public IPacket {
public:
    VersionNegotiationPacket();
    virtual ~VersionNegotiationPacket();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_NEGOTIATION; }

private:
    LongHeader _header;
    std::vector<uint32_t> _support_version;
};

}

#endif