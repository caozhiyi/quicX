
#ifndef QUIC_PACKET_RTT_0_PACKET
#define QUIC_PACKET_RTT_0_PACKET

#include "quic/packet/long_header.h"

namespace quicx {

class Rtt0Packet: public LongHeader {
public:
    Rtt0Packet();
    virtual ~Rtt0Packet();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<Frame> frame);

private:
    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif