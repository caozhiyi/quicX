
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include <memory>
#include "quic/packet/type.h"
#include "quic/frame/frame_interface.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

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

    uint32_t GetPayloadLength() { return _payload_length; }

private:
    LongHeader _header;
    uint32_t _token_length;
    const uint8_t* _token;

    uint32_t _payload_length;

    uint32_t _packet_num_offset;

    std::vector<std::shared_ptr<IFrame>> _frame_list;
};

}

#endif