#ifndef QUIC_PACKET_HEADER_HEADER_INTERFACE
#define QUIC_PACKET_HEADER_HEADER_INTERFACE


#include <cstdint>
#include "quic/packet/header/header_flag.h"

namespace quicx {

class IBufferRead;
class IBufferWrite;
class IHeader:
    public HeaderFlag {
public:
    IHeader() {}
    IHeader(uint8_t flag): HeaderFlag(flag) {}
    virtual ~IHeader() {}

    virtual bool EncodeHeader(std::shared_ptr<IBufferWrite> buffer) = 0;
    virtual bool DecodeHeader(std::shared_ptr<IBufferRead> buffer, bool with_flag = false) = 0;
    virtual uint32_t EncodeHeaderSize() = 0;
    
protected:
    std::pair<const uint8_t*, const uint8_t*> _header_src_data;
};

}

#endif