#ifndef QUIC_CONNECTION_PACKET_VISITOR_INTERFACE
#define QUIC_CONNECTION_PACKET_VISITOR_INTERFACE

#include "quic/packet/if_packet.h"
#include "common/buffer/if_buffer.h"

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