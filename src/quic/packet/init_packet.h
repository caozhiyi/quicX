
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/frame/frame_interface.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

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
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer);
    virtual bool DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual uint32_t GetPacketNumOffset() { return _packet_num_offset; }

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _frame_list; }

    void SetToken(uint8_t* token, uint32_t len);
    uint32_t GetTokenLength() { return _token_length; }
    uint8_t* GetToken() { return _token; }

    void SetPayload(BufferSpan payload);
    BufferSpan GetPayload() { return _palyload; }
    uint32_t GetPayloadLength() { return _payload_length; }

private:
    LongHeader _header;
    uint32_t _token_length;
    uint8_t* _token;

    uint32_t _payload_length;
    uint64_t _packet_num;
    BufferSpan _palyload;

    uint32_t _payload_offset;
    uint32_t _packet_num_offset;
    std::vector<std::shared_ptr<IFrame>> _frame_list;
};

}

#endif