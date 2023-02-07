
#ifndef QUIC_PACKET_HEADER_SHORT_PACKET
#define QUIC_PACKET_HEADER_SHORT_PACKET

#include <memory>
#include "quic/common/constants.h"
#include "quic/packet/header/header_interface.h"

namespace quicx {

class ShortHeader:
    public IHeader {
public:
    ShortHeader();
    ShortHeader(uint8_t flag);
    virtual ~ShortHeader();

    virtual bool EncodeHeader(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeHeader(std::shared_ptr<IBufferRead> buffer, bool with_flag = false);
    virtual uint32_t EncodeHeaderSize();
};

}

#endif