
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include <memory>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header_interface.h"
#include "common/buffer/buffer_read_view.h"

namespace quicx {

class InitPacket:
    public IPacket {
public:
    InitPacket(std::shared_ptr<IHeader> header);
    virtual ~InitPacket();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_INITIAL; }
    //virtual BufferReadView& GetPayload();

private:
    uint32_t _token_length;
    const uint8_t* _token;

    uint32_t _payload_length;
    //BufferReadView _payload;     /*encryption protection*/
};

}

#endif