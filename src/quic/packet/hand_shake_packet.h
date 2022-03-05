
#ifndef QUIC_PACKET_HAND_SHAKE_PACKET
#define QUIC_PACKET_HAND_SHAKE_PACKET

#include <memory>
#include "quic/packet/long_header.h"

namespace quicx {

class HandShakePacket: public LongHeader {
public:
    HandShakePacket();
    virtual ~HandShakePacket();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<Frame> frame);
};

}

#endif