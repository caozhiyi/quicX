#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>
#include "common/buffer/buffer_interface.h"

namespace quicx {

class IFrame;
class Buffer;
class AlloterWrap;

class Packet {
public:
    Packet() {}
    virtual ~Packet() {}

    virtual bool Encode(std::shared_ptr<IBufferWriteOnly> buffer) = 0;
    virtual bool Decode(std::shared_ptr<IBufferReadOnly> buffer, bool with_type = false) = 0;
    virtual uint32_t EncodeSize() = 0;

    virtual bool AddFrame(std::shared_ptr<IFrame> frame) = 0;
};

}

#endif