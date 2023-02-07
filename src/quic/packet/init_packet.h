
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include <memory>
#include "quic/packet/packet_interface.h"
#include "quic/packet/header/long_header.h"

namespace quicx {

class InitPacket:
    public IPacket {
public:
    InitPacket();
    InitPacket(uint8_t flag);
    virtual ~InitPacket();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeSize();

    virtual IHeader* GetHeader() { return &_header; }

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

private:
    LongHeader _header;
    uint32_t _token_length;
    const uint8_t* _token;

    uint32_t _payload_length;
    uint8_t* _payload;     /*encryption protection*/
};

}

#endif