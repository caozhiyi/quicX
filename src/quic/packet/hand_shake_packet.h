
#ifndef QUIC_PACKET_HAND_SHAKE_PACKET
#define QUIC_PACKET_HAND_SHAKE_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class HandShakePacket:
    public IPacket {
public:
    HandShakePacket();
    HandShakePacket(uint8_t flag);
    virtual ~HandShakePacket();

    virtual uint16_t GetCryptoLevel() const { return PCL_HANDSHAKE; }
    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeBeforeDecrypt(std::shared_ptr<IBufferRead> buffer);
    virtual bool DecodeAfterDecrypt(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }
    virtual uint32_t GetPacketNumOffset() { return _packet_num_offset; }

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);
    virtual std::vector<std::shared_ptr<IFrame>>& GetFrames() { return _frame_list; }

    void SetPayload(BufferSpan payload);
    BufferSpan GetPayload() { return _palyload; }
    uint32_t GetPayloadLength() { return _payload_length; }

private:
    LongHeader _header;
    uint32_t _payload_length;
    uint64_t _packet_num;
    BufferSpan _palyload;

    uint32_t _payload_offset;
    uint32_t _packet_num_offset;
    std::vector<std::shared_ptr<IFrame>> _frame_list;
};

}

#endif