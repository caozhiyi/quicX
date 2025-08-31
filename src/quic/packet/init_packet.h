
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/frame/if_frame.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {

/*
Initial Packet {
    Header Form (1) = 1,
    Fixed Bit (1) = 1,
    Long Packet Type (2) = 0,
    Reserved Bits (2),
    Packet Number Length (2),
    Version (32),
    Destination Connection ID Length (8),
    Destination Connection ID (0..160),
    Source Connection ID Length (8),
    Source Connection ID (0..160),
    Token Length (i),
    Token (..),
    Length (i),
    Packet Number (8..32),
    Packet Payload (8..),
}
*/
class InitPacket:
    public IPacket {
public:
    InitPacket();
    InitPacket(uint8_t flag);
    virtual ~InitPacket();

    virtual uint16_t GetCryptoLevel() const { return PakcetCryptoLevel::kInitialCryptoLevel; }
    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer, bool with_flag = false);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer);

    virtual IHeader* GetHeader() { return &header_; }
    virtual uint32_t GetPacketNumOffset() { return packet_num_offset_; }
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return frames_list_; }

    void SetToken(uint8_t* token, uint32_t len);
    uint32_t GetTokenLength() { return token_length_; }
    uint8_t* GetToken() { return token_; }

    void SetPayload(common::BufferSpan payload);
    common::BufferSpan GetPayload() { return payload_; }
    uint32_t GetLength() { return length_; }

private:
    LongHeader header_;
    uint32_t token_length_;
    uint8_t* token_;

    uint32_t length_;
    common::BufferSpan payload_;

    uint32_t payload_offset_;
    uint32_t packet_num_offset_;
    std::vector<std::shared_ptr<IFrame>> frames_list_;
};

}
}

#endif