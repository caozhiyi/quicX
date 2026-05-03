#include <cstring>

#include "common/buffer/if_buffer.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace common {

BufferDecodeWrapper::BufferDecodeWrapper(std::shared_ptr<IBuffer> buffer)
    : buffer_(buffer),
      is_contiguous_(buffer->GetChunkCount() <= 1),
      flushed_(false),
      start_(nullptr),
      pos_(nullptr),
      end_(nullptr) {
    if (is_contiguous_) {
        auto span = buffer_->GetReadableSpan();
        start_ = span.GetStart();
        pos_ = start_;
        end_ = span.GetEnd();
    } else {
        reader_.Reset(buffer_);
    }
}

BufferDecodeWrapper::~BufferDecodeWrapper() {
    if (!flushed_) {
        Flush();
    }
}

void BufferDecodeWrapper::Flush() {
    flushed_ = true;
    if (is_contiguous_) {
        buffer_->MoveReadPt(static_cast<uint32_t>(pos_ - start_));
        start_ = pos_;
    } else {
        reader_.Sync();
    }
}

void BufferDecodeWrapper::CancelDecode() {
    flushed_ = true;
    if (is_contiguous_) {
        pos_ = start_;
    } else {
        reader_.Reset(buffer_);
    }
}

bool BufferDecodeWrapper::DecodeFixedUint8(uint8_t& value) {
    if (is_contiguous_) {
        uint8_t* new_pos = common::FixedDecodeUint8(pos_, end_, value);
        if (!new_pos) return false;
        flushed_ = false;
        pos_ = new_pos;
        return true;
    }

    uint8_t buf[1];
    uint32_t actual = 0;
    if (!SlowReadForDecode(buf, 1, actual) || actual < 1) return false;
    uint8_t* decoded = common::FixedDecodeUint8(buf, buf + 1, value);
    if (!decoded) return false;
    reader_.MoveReadPt(1);
    flushed_ = false;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint16(uint16_t& value) {
    if (is_contiguous_) {
        uint8_t* new_pos = common::FixedDecodeUint16(pos_, end_, value);
        if (!new_pos) return false;
        flushed_ = false;
        pos_ = new_pos;
        return true;
    }

    uint8_t buf[2];
    uint32_t actual = 0;
    if (!SlowReadForDecode(buf, 2, actual) || actual < 2) return false;
    uint8_t* decoded = common::FixedDecodeUint16(buf, buf + 2, value);
    if (!decoded) return false;
    reader_.MoveReadPt(2);
    flushed_ = false;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint32(uint32_t& value) {
    if (is_contiguous_) {
        uint8_t* new_pos = common::FixedDecodeUint32(pos_, end_, value);
        if (!new_pos) return false;
        flushed_ = false;
        pos_ = new_pos;
        return true;
    }

    uint8_t buf[4];
    uint32_t actual = 0;
    if (!SlowReadForDecode(buf, 4, actual) || actual < 4) return false;
    uint8_t* decoded = common::FixedDecodeUint32(buf, buf + 4, value);
    if (!decoded) return false;
    reader_.MoveReadPt(4);
    flushed_ = false;
    return true;
}

bool BufferDecodeWrapper::DecodeFixedUint64(uint64_t& value) {
    if (is_contiguous_) {
        uint8_t* new_pos = common::FixedDecodeUint64(pos_, end_, value);
        if (!new_pos) return false;
        flushed_ = false;
        pos_ = new_pos;
        return true;
    }

    uint8_t buf[8];
    uint32_t actual = 0;
    if (!SlowReadForDecode(buf, 8, actual) || actual < 8) return false;
    uint8_t* decoded = common::FixedDecodeUint64(buf, buf + 8, value);
    if (!decoded) return false;
    reader_.MoveReadPt(8);
    flushed_ = false;
    return true;
}

bool BufferDecodeWrapper::DecodeBytes(uint8_t*& out, uint32_t len, bool copy) {
    if (is_contiguous_) {
        uint8_t* new_pos = nullptr;
        if (copy) {
            new_pos = common::DecodeBytesCopy(pos_, end_, out, len);
        } else {
            new_pos = common::DecodeBytesNoCopy(pos_, end_, out, len);
        }
        if (!new_pos) return false;
        flushed_ = false;
        pos_ = new_pos;
        return true;
    }

    // Slow path: always copy (no contiguous memory to point into).
    // NOTE: Caller takes ownership of the allocated memory via 'out' and must delete[] it.
    // This matches the ownership semantics of DecodeBytesCopy in the contiguous (fast) path.
    out = new (std::nothrow) uint8_t[len];
    if (!out) {
        return false;
    }
    uint32_t read = reader_.Read(reinterpret_cast<uint8_t*>(out), len);
    if (read < len) {
        delete[] out;
        out = nullptr;
        return false;
    }
    flushed_ = false;
    return true;
}

BufferSpan BufferDecodeWrapper::GetDataSpan() const {
    if (is_contiguous_) {
        return BufferSpan(start_, pos_);
    }
    return BufferSpan();
}

uint32_t BufferDecodeWrapper::GetDataLength() const {
    if (is_contiguous_) {
        return static_cast<uint32_t>(end_ - pos_);
    }
    return reader_.GetDataLength();
}

uint32_t BufferDecodeWrapper::GetReadLength() const {
    if (is_contiguous_) {
        return static_cast<uint32_t>(pos_ - start_);
    }
    return reader_.GetReadOffset();
}

std::shared_ptr<IBuffer> BufferDecodeWrapper::GetBuffer() const {
    return buffer_;
}

bool BufferDecodeWrapper::SlowReadForDecode(uint8_t* temp_buf, uint32_t needed, uint32_t& actual) {
    actual = reader_.ReadNotMovePt(temp_buf, needed);
    return actual >= needed;
}

}  // namespace common
}  // namespace quicx
