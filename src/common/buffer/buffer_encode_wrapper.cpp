#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace common {

BufferEncodeWrapper::BufferEncodeWrapper(std::shared_ptr<IBufferWrite> buffer):
    buffer_(buffer),
    flushed_(false) {
    auto span = buffer_->GetWriteSpan(); 
    pos_ = span.GetStart();
    end_ = span.GetEnd();
}

BufferEncodeWrapper::~BufferEncodeWrapper() {
    if (!flushed_) {
        Flush();
    }
}

void BufferEncodeWrapper::Flush() {
    flushed_ = true;
    buffer_->MoveWritePt(pos_ - buffer_->GetWriteSpan().GetStart());
}

bool BufferEncodeWrapper::EncodeFixedUint8(uint8_t value) {
    pos_ = common::FixedEncodeUint8(pos_, value);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

bool BufferEncodeWrapper::EncodeFixedUint16(uint16_t value) {
    pos_ = common::FixedEncodeUint16(pos_, value);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

bool BufferEncodeWrapper::EncodeFixedUint32(uint32_t value) {
    pos_ = common::FixedEncodeUint32(pos_, value);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

bool BufferEncodeWrapper::EncodeFixedUint64(uint64_t value) {
    pos_ = common::FixedEncodeUint64(pos_, value);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

bool BufferEncodeWrapper::EncodeBytes(uint8_t* in, uint32_t len) {
    pos_ = common::EncodeBytes(pos_, end_, in, len);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}


} // namespace common
} // namespace quicx