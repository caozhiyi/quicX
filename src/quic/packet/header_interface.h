#ifndef QUIC_PACKET_HEADER_INTERFACE
#define QUIC_PACKET_HEADER_INTERFACE


#include <cstdint>
#include "quic/packet/header_flag.h"

namespace quicx {

class IBufferReadOnly;
class IBufferWriteOnly;
class IHeader {
public:
    IHeader() {}
    IHeader(HeaderFlag flag): _flag(flag) {}
    virtual ~IHeader() {}

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer) = 0;
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_flag = false) = 0;
    virtual uint32_t EncodeSize() = 0;

    virtual HeaderFlag& GetHeaderFlag() { return _flag; }

protected:
    HeaderFlag _flag;
};

}

#endif