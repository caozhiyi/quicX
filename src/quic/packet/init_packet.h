
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include <memory>
#include "common/buffer/buffer_view.h"
#include "quic/packet/packet_interface.h"
#include "quic/packet/header_interface.h"

namespace quicx {

class InitPacket:
    public IPacket {
public:
    InitPacket(std::shared_ptr<IHeader> header);
    virtual ~InitPacket();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

    virtual PacketType GetPacketType() { return PT_INITIAL; }
    virtual BufferView& GetPayload();

private:
    uint32_t _token_length;
    char* _token;

    uint32_t _payload_length;
    uint32_t _packet_number; /*encryption protection*/
    BufferView _payload;     /*encryption protection*/

    std::shared_ptr<IBufferReadOnly> _buffer;
};

}

#endif