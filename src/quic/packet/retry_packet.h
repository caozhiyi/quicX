
#ifndef QUIC_PACKET_RETRY_PACKET
#define QUIC_PACKET_RETRY_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class RetryPacket:
    public IPacket {
public:
    RetryPacket();
    RetryPacket(uint8_t flag);
    virtual ~RetryPacket();

    virtual uint16_t GetCryptoLevel() const { return PCL_UNCRYPTO; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_RETRY; }

private:
    LongHeader _header;
    char* _retry_token;
    char _retry_integrity_tag[128];
};

}

#endif