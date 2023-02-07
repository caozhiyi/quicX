
#ifndef QUIC_PACKET_RTT_0_PACKET
#define QUIC_PACKET_RTT_0_PACKET

#include <memory>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class Rtt0Packet:
    public IPacket {
public:
    Rtt0Packet();
    virtual ~Rtt0Packet();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_0RTT; }

private:
    LongHeader _header;
    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif