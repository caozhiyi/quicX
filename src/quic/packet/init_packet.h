
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include "long_header_packet.h"

namespace quicx {

class InitPacket: public LongHeaderPacket {
public:
    InitPacket();
    virtual ~InitPacket();

private:
    uint32_t _toekn_length;
    char* _token;

    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif