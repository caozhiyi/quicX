
#ifndef QUIC_PACKET_RTT_1_PACKET
#define QUIC_PACKET_RTT_1_PACKET

#include <memory>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header_interface.h"

namespace quicx {

class Rtt1Packet:
    public IPacket {
public:
    Rtt1Packet();
    Rtt1Packet(std::shared_ptr<IHeader> header);
    virtual ~Rtt1Packet();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_header = false);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

protected:
    char _destination_connection_id[__max_connection_length];

    uint32_t _packet_number_length;
    char* _packet_number;

    char* _payload;
};

}

#endif