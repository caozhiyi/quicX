#include "common/buffer/buffer.h"
#include "quic/stream/fix_buffer_frame_visitor.h"


namespace quicx {

FixBufferFrameVisitor::FixBufferFrameVisitor(uint32_t size) {
    _buffer_start = new uint8_t[size];
    _buffer = std::make_shared<Buffer>(_buffer_start, _buffer_start + size);
}

FixBufferFrameVisitor::~FixBufferFrameVisitor() {
    delete[] _buffer_start;
}

bool FixBufferFrameVisitor::HandleFrame(std::shared_ptr<IFrame> frame) {
    _types.push_back(FrameType(frame->GetType()));
    return frame->Encode(_buffer);
}

uint32_t FixBufferFrameVisitor::GetLeftSize() {
    return _buffer->GetFreeLength();
}

std::shared_ptr<IBuffer> FixBufferFrameVisitor::GetBuffer() {
    return _buffer;
}

std::vector<FrameType>& FixBufferFrameVisitor::GetFramesType() {
    return _types;
}

}