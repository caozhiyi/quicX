
#ifndef QUIC_PACKET_RTT_1_PACKET
#define QUIC_PACKET_RTT_1_PACKET

#include <memory>
#include "quic/common/constants.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/short_header.h"

namespace quicx {

class Rtt1Packet:
    public IPacket {
public:
    Rtt1Packet();
    virtual ~Rtt1Packet();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_0RTT; }

protected:
    ShortHeader _header;
    char _destination_connection_id[__max_connection_length];

    uint32_t _packet_number_length;
    char* _packet_number;

    char* _payload;
};

}

#endif