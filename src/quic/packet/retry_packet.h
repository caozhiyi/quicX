
#ifndef QUIC_PACKET_RETRY_PACKET
#define QUIC_PACKET_RETRY_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "common/buffer/buffer_span.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

/*
Retry Packet {
    Header Form (1) = 1,
    Fixed Bit (1) = 1,
    Long Packet Type (2) = 3,
    Unused (4),
    Version (32),
    Destination Connection ID Length (8),
    Destination Connection ID (0..160),
    Source Connection ID Length (8),
    Source Connection ID (0..160),
    Retry Token (..),
    Retry Integrity Tag (128),
}
*/

class RetryPacket:
    public IPacket {
public:
    RetryPacket();
    RetryPacket(uint8_t flag);
    virtual ~RetryPacket();

    virtual uint16_t GetCryptoLevel() const { return PCL_UNCRYPTO; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<IBufferRead> buffer);
    virtual bool DecodeWithCrypto(std::shared_ptr<IBuffer> buffer) { return true; }

    virtual IHeader* GetHeader() { return &_header; }

    void SetRetryToken(BufferSpan toekn) { _retry_token = toekn; }
    BufferSpan& GetRetryToken() { return _retry_token; }

    void SetRetryIntegrityTag(uint8_t* tag);
    uint8_t* GetRetryIntegrityTag();

private:
    LongHeader _header;
    BufferSpan _retry_token;
    uint8_t _retry_integrity_tag[__retry_integrity_tag_length];
};

}

#endif