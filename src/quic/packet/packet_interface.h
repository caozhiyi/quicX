#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>
#include "quic/packet/type.h"
#include "common/buffer/buffer_view.h"
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

    virtual PacketType GetPacketType() = 0;
    virtual BufferView& GetPayload() { return _view; }

    std::shared_ptr<IHeader> GetHeader() { return _header; }

protected:
    std::shared_ptr<IHeader> _header;
    static BufferView _view;
};

}

#endif