#ifndef QUIC_PACKET_HEADER_IF_HEADER
#define QUIC_PACKET_HEADER_IF_HEADER


#include <cstdint>
#include "common/buffer/buffer_span.h"
#include "quic/packet/header/header_flag.h"
#include "common/buffer/buffer_read_interface.h"
#include "common/buffer/buffer_write_interface.h"

namespace quicx {
namespace quic {

class IHeader:
    public HeaderFlag {
public:
    IHeader() {}
    IHeader(PacketHeaderType type): HeaderFlag(type) {}
    IHeader(uint8_t flag): HeaderFlag(flag) {}
    virtual ~IHeader() {}

    virtual bool EncodeHeader(std::shared_ptr<common::IBufferWrite> buffer) = 0;
    virtual bool DecodeHeader(std::shared_ptr<common::IBufferRead> buffer, bool with_flag = false) = 0;
    virtual uint32_t EncodeHeaderSize() = 0;

    virtual void SetDestinationConnectionId(uint8_t* id, uint8_t len) = 0;
    virtual uint8_t GetDestinationConnectionIdLength() = 0;
    
    virtual common::BufferSpan& GetHeaderSrcData() { return header_src_data_; }
protected:
    common::BufferSpan header_src_data_;
};

}
}

#endif