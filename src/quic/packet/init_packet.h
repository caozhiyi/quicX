
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/frame/if_frame.h"
#include "quic/packet/packet_interface.h"
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

    virtual uint16_t GetCryptoLevel() const { return PCL_INITIAL; }
    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer);

    virtual IHeader* GetHeader() { return &_header; }
    virtual uint32_t GetPacketNumOffset() { return _packet_num_offset; }
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _frames_list; }

    void SetToken(uint8_t* token, uint32_t len);
    uint32_t GetTokenLength() { return _token_length; }
    uint8_t* GetToken() { return _token; }

    void SetPayload(common::BufferSpan payload);
    common::BufferSpan GetPayload() { return _payload; }
    uint32_t GetLength() { return _length; }

private:
    LongHeader _header;
    uint32_t _token_length;
    uint8_t* _token;

    uint32_t _length;
    common::BufferSpan _payload;

    uint32_t _payload_offset;
    uint32_t _packet_num_offset;
    std::vector<std::shared_ptr<IFrame>> _frames_list;
};

}
}

#endif