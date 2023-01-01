
#ifndef QUIC_PACKET_SHORT_PACKET
#define QUIC_PACKET_SHORT_PACKET

#include <memory>
#include "quic/common/constants.h"
#include "quic/packet/header_interface.h"

namespace quicx {

class ShortHeader:
    public IHeader {
public:
    ShortHeader();
    ShortHeader(HeaderFlag flag);
    virtual ~ShortHeader();

    virtual bool Encode(std::shared_ptr<IBufferWrite> buffer);
    virtual bool Decode(std::shared_ptr<IBufferRead> buffer, bool with_flag = false);
    virtual uint32_t EncodeSize();
};

}

#endif