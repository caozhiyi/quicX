
#ifndef QUIC_PACKET_HANDSHAKE_PACKET
#define QUIC_PACKET_HANDSHAKE_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {
namespace quic {

class HandshakePacket:
    public IPacket {
public:
    HandshakePacket();
    HandshakePacket(uint8_t flag);
    virtual ~HandshakePacket();

    virtual uint16_t GetCryptoLevel() const { return PCL_HANDSHAKE; }
    virtual bool Encode(std::shared_ptr<common::IBufferWrite> buffer);
    virtual bool DecodeWithoutCrypto(std::shared_ptr<common::IBufferRead> buffer);
    virtual bool DecodeWithCrypto(std::shared_ptr<common::IBuffer> buffer);

    virtual IHeader* GetHeader() { return &_header; }
    virtual uint32_t GetPacketNumOffset() { return _packet_num_offset; }

    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _frames_list; }

    void SetPayload(common::BufferSpan payload);
    common::BufferSpan GetPayload() { return _payload; }
    uint32_t GetLength() { return _length; }

private:
    LongHeader _header;
    uint32_t _length;
    common::BufferSpan _payload;

    uint32_t _payload_offset;
    uint32_t _packet_num_offset;
    std::vector<std::shared_ptr<IFrame>> _frames_list;
};

}
}

#endif