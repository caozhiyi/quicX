#ifndef QUIC_CONNECTION_FIX_BUFFER_PACKET_VISITOR
#define QUIC_CONNECTION_FIX_BUFFER_PACKET_VISITOR

#include "quic/connection/packet_visitor_interface.h"

namespace quicx {

class FixBufferPacketVisitor:
    public IPacketVisitor {
public:
    FixBufferPacketVisitor(uint32_t size);
    virtual ~FixBufferPacketVisitor();

    virtual bool HandlePacket(std::shared_ptr<IPacket> packet);

    virtual uint32_t GetLeftSize();

    virtual std::shared_ptr<IBuffer> GetBuffer();

private:
    uint8_t* _buffer_start;
    std::shared_ptr<IBuffer> _buffer;
};


}

#endif