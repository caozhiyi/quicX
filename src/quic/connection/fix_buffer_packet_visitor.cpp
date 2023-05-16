#include "common/buffer/buffer.h"
#include "quic/connection/fix_buffer_packet_visitor.h"

namespace quicx {

FixBufferPacketVisitor::FixBufferPacketVisitor(uint32_t size) {
    uint8_t* _buffer_start = new uint8_t[size];
    _buffer = std::make_shared<Buffer>(_buffer_start, _buffer_start + size);
}

FixBufferPacketVisitor::~FixBufferPacketVisitor() {
    delete[] _buffer_start;
}

bool FixBufferPacketVisitor::HandlePacket(std::shared_ptr<IPacket> packet) {
    return packet->Encode(_buffer);
}

uint32_t FixBufferPacketVisitor::GetLeftSize() {
    return _buffer->GetFreeLength();
}

std::shared_ptr<IBuffer> FixBufferPacketVisitor::GetBuffer() {
    return _buffer;
}

}
