#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace common {

// Snapshot the readable span of the provided buffer. Callers should avoid
// mutating the buffer while the wrapper is alive.
BufferDecodeWrapper::BufferDecodeWrapper(std::shared_ptr<IBuffer> buffer):
    buffer_(buffer),
    flushed_(false) {
    auto span = buffer_->GetReadableSpan();
    pos_ = span.GetStart();
    start_ = pos_;
    end_ = span.GetEnd();
}

// Commit any consumed bytes back to the underlying buffer.
BufferDecodeWrapper::~BufferDecodeWrapper() {
    if (!flushed_) {
        Flush();
    }
}

void BufferDecodeWrapper::Flush() {
    flushed_ = true;
    buffer_->MoveReadPt(pos_ - start_);
    // Update start_ to current position for subsequent operations
    start_ = pos_;
}

void BufferDecodeWrapper::CancelDecode() {
    flushed_ = true;
}

bool BufferDecodeWrapper::DecodeFixedUint8(uint8_t& value) {
    uint8_t* new_pos = common::FixedDecodeUint8(pos_, end_, value);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint16(uint16_t& value) {
    uint8_t* new_pos = common::FixedDecodeUint16(pos_, end_, value);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint32(uint32_t& value) {
    uint8_t* new_pos = common::FixedDecodeUint32(pos_, end_, value);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint64(uint64_t& value) {
    uint8_t* new_pos = common::FixedDecodeUint64(pos_, end_, value);
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

bool BufferDecodeWrapper::DecodeBytes(uint8_t*& out, uint32_t len, bool copy) {
    uint8_t* new_pos = nullptr;
    if (copy) {
        new_pos = common::DecodeBytesCopy(pos_, end_, out, len);
    } else {
        new_pos = common::DecodeBytesNoCopy(pos_, end_, out, len);
    }
    if (!new_pos) {
        return false;
    }
    flushed_ = false;
    pos_ = new_pos;
    return true;
}

common::BufferSpan BufferDecodeWrapper::GetDataSpan() const {
    return common::BufferSpan(buffer_->GetReadableSpan().GetStart(), pos_);
}

uint32_t BufferDecodeWrapper::GetDataLength() const {
    return buffer_->GetReadableSpan().GetEnd() - pos_;
}

uint32_t BufferDecodeWrapper::GetReadLength() const {
    return pos_ - start_;
}
}  // namespace common
}  // namespace quicx