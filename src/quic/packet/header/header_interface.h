#ifndef QUIC_PACKET_HEADER_HEADER_INTERFACE
#define QUIC_PACKET_HEADER_HEADER_INTERFACE


#include <cstdint>
#include "common/buffer/buffer_span.h"
#include "quic/packet/header/header_flag.h"
#include "common/buffer/buffer_read_interface.h"
#include "common/buffer/buffer_write_interface.h"

namespace quicx {

class IHeader:
    public HeaderFlag {
public:
    IHeader() {}
    IHeader(PacketHeaderType type): HeaderFlag(type) {}
    IHeader(uint8_t flag): HeaderFlag(flag) {}
    virtual ~IHeader() {}

    virtual bool EncodeHeader(std::shared_ptr<IBufferWrite> buffer) = 0;
    virtual bool DecodeHeader(std::shared_ptr<IBufferRead> buffer, bool with_flag = false) = 0;
    virtual uint32_t EncodeHeaderSize() = 0;
    
    virtual BufferSpan& GetHeaderSrcData() { return _header_src_data; }
protected:
    BufferSpan _header_src_data;
};

}

#endif