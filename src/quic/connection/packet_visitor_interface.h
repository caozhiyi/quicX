#ifndef QUIC_CONNECTION_PACKET_VISITOR_INTERFACE
#define QUIC_CONNECTION_PACKET_VISITOR_INTERFACE

#include "quic/packet/packet_interface.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {
namespace quic {

class IPacketVisitor {
public:
    IPacketVisitor() {}
    virtual ~IPacketVisitor() {}

    virtual bool HandlePacket(std::shared_ptr<IPacket> packet) = 0;

    virtual uint32_t GetLeftSize() = 0;

    virtual std::shared_ptr<common::IBuffer> GetBuffer() = 0;
};

}
}

#endif