
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
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher = nullptr);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual bool Decode(std::shared_ptr<ICryptographer> crypto_grapher, std::shared_ptr<IBuffer> buffer);

    virtual IHeader* GetHeader() { return &_header; }
    virtual uint32_t GetPacketNumOffset() { return 0; }
    virtual bool AddFrame(std::shared_ptr<IFrame> frame);
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { }

    virtual PacketType GetPacketType() { return PT_RETRY; }

private:
    LongHeader _header;
    char* _retry_token;
    char _retry_integrity_tag[128];
};

}

#endif