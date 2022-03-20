
#ifndef QUIC_PACKET_RTT_1_PACKET
#define QUIC_PACKET_RTT_1_PACKET

#include <memory>
#include "quic/packet/short_header.h"

namespace quicx {

class Rtt1Packet: public ShortHeader {
public:
    Rtt1Packet();
    virtual ~Rtt1Packet();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);
};

}

#endif