
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include <memory>
#include "quic/packet/long_header.h"

namespace quicx {

class InitPacket: public LongHeader {
public:
    InitPacket();
    virtual ~InitPacket();

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<IFrame> frame);

private:
    uint32_t _token_length;
    char* _token;

    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif