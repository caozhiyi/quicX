#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace common {

// Capture the writable span of the supplied buffer. We assume the caller won't
// mutate the buffer concurrently while the wrapper is alive.
BufferEncodeWrapper::BufferEncodeWrapper(std::shared_ptr<IBuffer> buffer):
    buffer_(buffer),
    flushed_(false) {
    auto span = buffer_->GetWritableSpan();
    pos_ = span.GetStart();
    start_ = pos_;
    end_ = span.GetEnd();
}

// Ensure any staged bytes are accounted for in the underlying buffer.
BufferEncodeWrapper::~BufferEncodeWrapper() {
    if (!flushed_) {
        Flush();
    }
}

void BufferEncodeWrapper::Flush() {
    flushed_ = true;
    buffer_->MoveWritePt(pos_ - start_);
    // Update start_ to current position for subsequent operations
    start_ = pos_;
}

bool BufferEncodeWrapper::EncodeFixedUint8(uint8_t value) {
    // The encode helpers return nullptr when the output range overflows.
    uint8_t* new_pos = common::FixedEncodeUint8(pos_, end_, value);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

bool BufferEncodeWrapper::EncodeFixedUint16(uint16_t value) {
    uint8_t* new_pos = common::FixedEncodeUint16(pos_, end_, value);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

bool BufferEncodeWrapper::EncodeFixedUint32(uint32_t value) {
    uint8_t* new_pos = common::FixedEncodeUint32(pos_, end_, value);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

bool BufferEncodeWrapper::EncodeFixedUint64(uint64_t value) {
    uint8_t* new_pos = common::FixedEncodeUint64(pos_, end_, value);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

bool BufferEncodeWrapper::EncodeBytes(uint8_t* in, uint32_t len) {
    uint8_t* new_pos = common::EncodeBytes(pos_, end_, in, len);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

common::BufferSpan BufferEncodeWrapper::GetDataSpan() const {
    return common::BufferSpan(buffer_->GetWritableSpan().GetStart(), pos_);
}

uint32_t BufferEncodeWrapper::GetDataLength() const {
    return pos_ - buffer_->GetWritableSpan().GetStart();
}

}  // namespace common
}  // namespace quicx