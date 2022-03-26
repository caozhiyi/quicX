
#ifndef QUIC_PACKET_HAND_SHAKE_PACKET
#define QUIC_PACKET_HAND_SHAKE_PACKET

#include <memory>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header_interface.h"

namespace quicx {

class HandShakePacket:
    public IPacket {
public:
    HandShakePacket();
    HandShakePacket(std::shared_ptr<IHeader> header);
    virtual ~HandShakePacket();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

private:
    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif