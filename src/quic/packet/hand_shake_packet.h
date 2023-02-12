
#ifndef QUIC_PACKET_HAND_SHAKE_PACKET
#define QUIC_PACKET_HAND_SHAKE_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class HandShakePacket:
    public IPacket {
public:
    HandShakePacket();
    virtual ~HandShakePacket();

    virtual uint16_t GetCryptoLevel() const { return PCL_HANDSHAKE; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_HANDSHAKE; }

private:
    LongHeader _header;
    uint32_t _payload_length;
    char* _payload;
};

}

#endif