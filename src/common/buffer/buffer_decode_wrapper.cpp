#include "buffer_decode_wrapper.h"

namespace quicx {
namespace common {

BufferDecodeWrapper::BufferDecodeWrapper(std::shared_ptr<IBufferRead> buffer):
    buffer_(buffer), 
    flushed_(false) {
    auto span = buffer_->GetReadSpan();
    pos_ = span.GetStart();
    end_ = span.GetEnd();
}

BufferDecodeWrapper::~BufferDecodeWrapper() {
    if (!flushed_) {
        Flush();
    }
}

void BufferDecodeWrapper::Flush() {
    flushed_ = true;
    buffer_->MoveReadPt(pos_ - buffer_->GetReadSpan().GetStart());
}

bool BufferDecodeWrapper::DecodeFixedUint8(uint8_t& value) {
    pos_ = common::FixedDecodeUint8(pos_, end_, value);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint16(uint16_t& value) {
    pos_ = common::FixedDecodeUint16(pos_, end_, value);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint32(uint32_t& value) {
    pos_ = common::FixedDecodeUint32(pos_, end_, value);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint64(uint64_t& value) {
    pos_ = common::FixedDecodeUint64(pos_, end_, value);
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

bool BufferDecodeWrapper::DecodeBytes(uint8_t*& out, uint32_t len, bool copy) {
    if (copy) {
        pos_ = common::DecodeBytesCopy(pos_, end_, out, len);
    } else {
        pos_ = common::DecodeBytesNoCopy(pos_, end_, out, len);
    }
    if (!pos_) {
        return false;
    }
    flushed_ = false;
    return true;
}

}
} 