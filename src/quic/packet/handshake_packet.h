
#ifndef QUIC_PACKET_HANDSHAKE_PACKET
#define QUIC_PACKET_HANDSHAKE_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class HandshakePacket:
    public IPacket {
public:
    HandshakePacket();
    HandshakePacket(uint8_t flag);
    virtual ~HandshakePacket();

    virtual uint16_t GetCryptoLevel() const { return PCL_HANDSHAKE; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer, std::shared_ptr<ICryptographer> crypto_grapher = nullptr);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual bool Decode(std::shared_ptr<IBuffer> buffer, std::shared_ptr<ICryptographer> crypto_grapher);

    virtual IHeader* GetHeader() { return &_header; }
    virtual uint32_t GetPacketNumOffset() { return _packet_num_offset; }

    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _frame_list; }

    void SetPayload(BufferSpan payload);
    BufferSpan GetPayload() { return _payload; }
    uint32_t GetLength() { return _length; }

private:
    LongHeader _header;
    uint32_t _length;
    BufferSpan _payload;

    uint32_t _payload_offset;
    uint32_t _packet_num_offset;
    std::vector<std::shared_ptr<IFrame>> _frame_list;
};

}

#endif