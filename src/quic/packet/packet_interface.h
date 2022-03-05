#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>

namespace quicx {

class Frame;
class Buffer;
class AlloterWrap;

class Packet {
public:
    Packet() {}
    virtual ~Packet() {}

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) = 0;
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter) = 0;
    virtual uint32_t EncodeSize() = 0;

    virtual bool AddFrame(std::shared_ptr<Frame> frame) = 0;
};

}

#endif