
#ifndef QUIC_PACKET_RTT_1_PACKET
#define QUIC_PACKET_RTT_1_PACKET

#include <memory>
#include "quic/packet/short_header.h"

namespace quicx {

class Rtt1Packet: public ShortHeader {
public:
    Rtt1Packet();
    virtual ~Rtt1Packet();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<Frame> frame);
};

}

#endif