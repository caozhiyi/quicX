
#ifndef QUIC_PACKET_HANDSHAKE_PACKET
#define QUIC_PACKET_HANDSHAKE_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {

class HandshakePacket:
    public IPacket {
public:
    HandshakePacket();
    HandshakePacket(uint8_t flag);
    virtual ~HandshakePacket();

    virtual uint16_t GetCryptoLevel() const { return PakcetCryptoLevel::kHandshakeCryptoLevel; }
    virtual bool Encode(std::shared_ptr<common::IBuffer> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBuffer> buffer, bool with_flag = false);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer);

    virtual IHeader* GetHeader() { return &header_; }
    virtual uint32_t GetPacketNumOffset() { return packet_num_offset_; }

    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return frames_list_; }

    void SetPayload(const common::SharedBufferSpan& payload);
    common::SharedBufferSpan GetPayload() { return payload_; }
    uint32_t GetLength() { return length_; }

private:
    LongHeader header_;
    uint32_t length_;
    common::SharedBufferSpan payload_;

    uint32_t payload_offset_;
    uint32_t packet_num_offset_;
    std::vector<std::shared_ptr<IFrame>> frames_list_;
};

}
}

#endif