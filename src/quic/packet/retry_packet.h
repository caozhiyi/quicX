
#ifndef QUIC_PACKET_RETRY_PACKET
#define QUIC_PACKET_RETRY_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "common/buffer/buffer_span.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {

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

    virtual uint16_t GetCryptoLevel() const { return PakcetCryptoLevel::kUnknownCryptoLevel; }
    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer) { return true; }

    virtual IHeader* GetHeader() { return &header_; }

    void SetRetryToken(common::BufferSpan toekn) { retry_token_ = toekn; }
    common::BufferSpan& GetRetryToken() { return retry_token_; }

    void SetRetryIntegrityTag(uint8_t* tag);
    uint8_t* GetRetryIntegrityTag();

private:
    LongHeader header_;
    common::BufferSpan retry_token_;
    uint8_t retry_integrity_tag_[kRetryIntegrityTagLength];
};

}
}

#endif