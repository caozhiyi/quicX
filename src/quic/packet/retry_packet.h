
#ifndef QUIC_PACKET_RETRY_PACKET
#define QUIC_PACKET_RETRY_PACKET

#include "long_header_packet.h"

namespace quicx {

class RetryPacket: public LongHeaderPacket {
public:
    RetryPacket();
    virtual ~RetryPacket();

private:
    char* _retry_token;
    char _retry_integrity_tag[128];
};

}

#endif