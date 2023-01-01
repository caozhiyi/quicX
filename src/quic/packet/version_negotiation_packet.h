
#ifndef QUIC_PACKET_VERSION_NEGOTIATION_PACKET
#define QUIC_PACKET_VERSION_NEGOTIATION_PACKET

#include <memory>
#include <vector>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header_interface.h"

namespace quicx {

class VersionNegotiationPacket:
    public IPacket {
public:
    VersionNegotiationPacket(std::shared_ptr<IHeader> header);
    virtual ~VersionNegotiationPacket();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_NEGOTIATION; }

private:
    std::vector<uint32_t> _support_version;
};

}

#endif