#include "common/buffer/buffer.h"
#include "quic/process/fix_buffer_data_visitor.h"


namespace quicx {

FixBufferDataVisitor::FixBufferDataVisitor(uint32_t size) {
    uint8_t* _buffer_start = new uint8_t[size];
    _buffer = std::make_shared<Buffer>(_buffer_start, _buffer_start + size);
}

FixBufferDataVisitor::~FixBufferDataVisitor() {
    delete[] _buffer_start;
}

bool FixBufferDataVisitor::HandleFrame(std::shared_ptr<IFrame> frame) {
    return frame->Encode(_buffer);
}

uint32_t FixBufferDataVisitor::GetLeftSize() {
    return _buffer->GetFreeLength();
}

std::shared_ptr<IBuffer> FixBufferDataVisitor::GetBuffer() {
    return _buffer;
}

}
