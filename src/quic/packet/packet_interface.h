#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>
#include "common/buffer/buffer_interface.h"

namespace quicx {

class IFrame;
class IHeader;

class IPacket {
public:
    IPacket() {}
    IPacket(std::shared_ptr<IHeader> header): _header(header) {}
    virtual ~IPacket() {}

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer) = 0;
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer) = 0;
    virtual uint32_t EncodeSize() = 0;

protected:
    std::shared_ptr<IHeader> _header;
};

}

#endif