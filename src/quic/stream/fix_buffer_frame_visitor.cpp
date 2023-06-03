#include "common/buffer/buffer.h"
#include "quic/frame/crypto_frame.h"
#include "quic/stream/fix_buffer_frame_visitor.h"


namespace quicx {

FixBufferFrameVisitor::FixBufferFrameVisitor(uint32_t size):
    _encryption_level(EL_APPLICATION) {
    _buffer_start = new uint8_t[size];
    _buffer = std::make_shared<Buffer>(_buffer_start, _buffer_start + size);
}

FixBufferFrameVisitor::~FixBufferFrameVisitor() {
    delete[] _buffer_start;
}

bool FixBufferFrameVisitor::HandleFrame(std::shared_ptr<IFrame> frame) {
    if (frame->GetType() == FT_CRYPTO) {
        auto crypto_frame = std::dynamic_pointer_cast<CryptoFrame>(frame);
        _encryption_level = crypto_frame->GetEncryptionLevel();
    }

    //_types.push_back(FrameType(frame->GetType()));
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

uint8_t FixBufferFrameVisitor::GetEncryptionLevel() {
    return _encryption_level;
}

}