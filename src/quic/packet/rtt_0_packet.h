
#ifndef QUIC_PACKET_RTT_0_PACKET
#define QUIC_PACKET_RTT_0_PACKET

#include <memory>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header_interface.h"

namespace quicx {

class Rtt0Packet:
    public IPacket {
public:
    Rtt0Packet(std::shared_ptr<IHeader> header);
    virtual ~Rtt0Packet();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_0RTT; }

private:
    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif