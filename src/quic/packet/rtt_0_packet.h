
#ifndef QUIC_PACKET_RTT_0_PACKET
#define QUIC_PACKET_RTT_0_PACKET

#include "long_header_packet.h"

namespace quicx {

class Rtt0Packet: public LongHeaderPacket {
public:
    Rtt0Packet();
    virtual ~Rtt0Packet();

private:
    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif