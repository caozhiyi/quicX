#include <algorithm>
#include <cstring>
#include <cstdlib>

#include "common/log/log.h"
#include "common/buffer/buffer_span.h"
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/shared_buffer_span.h"
#include "common/buffer/single_block_buffer.h"

namespace quicx {
namespace common {

SingleBlockBuffer::SingleBlockBuffer() = default;

// Attach an existing chunk and initialize pointer state.
SingleBlockBuffer::SingleBlockBuffer(std::shared_ptr<IBufferChunk> chunk) {
    Reset(std::move(chunk));
}

SingleBlockBuffer::SingleBlockBuffer(SingleBlockBuffer&& other) noexcept {
    *this = std::move(other);
}

SingleBlockBuffer& SingleBlockBuffer::operator=(SingleBlockBuffer&& other) noexcept {
    if (this != &other) {
        chunk_ = std::move(other.chunk_);
        read_pos_ = other.read_pos_;
        write_pos_ = other.write_pos_;
        buffer_start_ = other.buffer_start_;
        buffer_end_ = other.buffer_end_;

        other.read_pos_ = nullptr;
        other.write_pos_ = nullptr;
        other.buffer_start_ = nullptr;
        other.buffer_end_ = nullptr;
    }
    return *this;
}

// Copy readable bytes into the caller-provided buffer without advancing read_pos_.
uint32_t SingleBlockBuffer::ReadNotMovePt(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        LOG_ERROR("data is nullptr");
        return 0;
    }
    return InnerRead(data, len, false);
}

// Move the read pointer forward (positive len) or backward (negative len).
uint32_t SingleBlockBuffer::MoveReadPt(int32_t len) {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return 0;
    }

    if (len > 0) {
        if (read_pos_ <= write_pos_) {
            size_t size = write_pos_ - read_pos_;
            if (static_cast<int32_t>(size) <= len) {
                Clear();
                return static_cast<uint32_t>(size);
            } else {
                read_pos_ += len;
                return static_cast<uint32_t>(len);
            }

        } else {
            LOG_ERROR("read_pos_ <= write_pos_ is false");
            return 0;
        }

    } else {
        len = -len;
        if (buffer_start_ <= read_pos_) {
            size_t size = read_pos_ - buffer_start_;
            if (static_cast<int32_t>(size) <= len) {
                read_pos_ -= size;
                return static_cast<uint32_t>(size);
            } else {
                read_pos_ -= len;
                return static_cast<uint32_t>(len);
            }
        } else {
            LOG_ERROR("read_pos_ <= write_pos_ is false");
            return 0;
        }
    }
}

// Copy readable bytes and advance read_pos_ by the amount consumed.
uint32_t SingleBlockBuffer::Read(uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        LOG_ERROR("data is nullptr");
        return 0;
    }
    return InnerRead(data, len, true);
}

void SingleBlockBuffer::VisitData(const std::function<void(uint8_t*, uint32_t)>& visitor) {
    if (!visitor) {
        return;
    }
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return;
    }
    uint32_t length = static_cast<uint32_t>(write_pos_ - read_pos_);
    if (length == 0) {
        return;
    }
    visitor(read_pos_, length);
}

void SingleBlockBuffer::VisitDataSpans(const std::function<void(SharedBufferSpan&)>& visitor) {
    if (!visitor) {
        return;
    }
    auto span = GetSharedReadableSpan();
    visitor(span);
}

// Return the number of bytes currently available for reading.
uint32_t SingleBlockBuffer::GetDataLength() {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return 0;
    }
    return static_cast<uint32_t>(write_pos_ - read_pos_);
}

// Return pointer to readable data, or nullptr if the buffer is invalid.
uint8_t* SingleBlockBuffer::GetData() const {
    return Valid() ? read_pos_ : nullptr;
}

// Produce a lightweight read-only window over the readable portion. The view
// is returned by value, so subsequent modifications to the buffer do not affect
// existing views.
BufferReadView SingleBlockBuffer::GetReadView() const {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return BufferReadView();
    }
    return BufferReadView(read_pos_, write_pos_);
}

// Expose a raw (non-owning) span over the readable range.
BufferSpan SingleBlockBuffer::GetWritableSpan() const {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return BufferSpan();
    }
    return BufferSpan(write_pos_, buffer_end_);
}

BufferSpan SingleBlockBuffer::GetReadableSpan() const {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return BufferSpan();
    }
    return BufferSpan(read_pos_, write_pos_);
}

// Expose a shared span that keeps the underlying chunk alive even if the
// buffer is reset or destroyed.
SharedBufferSpan SingleBlockBuffer::GetSharedBufferSpan() const {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return SharedBufferSpan();
    }
    return SharedBufferSpan(chunk_, buffer_start_, buffer_end_);
}

SharedBufferSpan SingleBlockBuffer::GetSharedReadableSpan() const {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return SharedBufferSpan();
    }
    return SharedBufferSpan(chunk_, read_pos_, write_pos_);
}

SharedBufferSpan SingleBlockBuffer::GetSharedReadableSpan(uint32_t length) const {
    return GetSharedReadableSpan(length, false);
}

SharedBufferSpan SingleBlockBuffer::GetSharedReadableSpan(uint32_t length, bool must_fill_length) const {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return SharedBufferSpan();
    }

    const uint32_t readable = static_cast<uint32_t>(write_pos_ - read_pos_);
    if (must_fill_length && readable < length) {
        LOG_WARN("readable length is less than required length");
        return SharedBufferSpan();
    }

    uint32_t span_len = readable;
    if (length != 0) {
        span_len = std::min(span_len, length);
    }
    return SharedBufferSpan(chunk_, read_pos_, span_len);
}

// Reset read/write pointers so the entire block becomes writable again.
void SingleBlockBuffer::Clear() {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return;
    }
    write_pos_ = read_pos_ = buffer_start_;
}

// Return the data as a string.
std::string SingleBlockBuffer::GetDataAsString() {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return "";
    }
    return std::string(read_pos_, write_pos_);
}

// Copy len bytes into the buffer at write_pos_ if space permits.
uint32_t SingleBlockBuffer::Write(const uint8_t* data, uint32_t len) {
    if (data == nullptr) {
        LOG_ERROR("data is nullptr");
        return 0;
    }
    return InnerWrite(data, len);
}

uint32_t SingleBlockBuffer::Write(std::shared_ptr<IBuffer> buffer) {
    if (!buffer) {
        return 0;
    }
    uint32_t written = 0;
    buffer->VisitData([&](uint8_t* data, uint32_t len) {
        if (len == 0) {
            return;
        }
        written += Write(data, len);
    });
    buffer->MoveReadPt(static_cast<int32_t>(written));
    return written;
}

uint32_t SingleBlockBuffer::Write(const SharedBufferSpan& span) {
    if (!span.Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }
    return Write(span.GetStart(), span.GetLength());
}

uint32_t SingleBlockBuffer::Write(const SharedBufferSpan& span, uint32_t data_len) {
    if (!span.Valid()) {
        LOG_ERROR("span is invalid");
        return 0;
    }
    uint32_t length = span.GetLength();
    if (data_len > 0) {
        length = std::min(length, data_len);
    }
    return Write(span.GetStart(), length);
}

// Remaining writable capacity in the backing buffer.
uint32_t SingleBlockBuffer::GetFreeLength() {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return 0;
    }
    return static_cast<uint32_t>(buffer_end_ - write_pos_);
}

BufferSpan SingleBlockBuffer::GetWritableSpan() {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return BufferSpan();
    }
    return BufferSpan(write_pos_, buffer_end_);
}

BufferSpan SingleBlockBuffer::GetWritableSpan(uint32_t expected_length) {
    if (!Valid() || expected_length == 0) {
        LOG_ERROR("buffer is invalid");
        return BufferSpan();
    }
    uint32_t free_len = static_cast<uint32_t>(buffer_end_ - write_pos_);
    if (free_len < expected_length) {
        return BufferSpan();
    }
    return BufferSpan(write_pos_, write_pos_ + expected_length);
}

// Move the write pointer forward (positive len) or backward (negative len).
uint32_t SingleBlockBuffer::MoveWritePt(int32_t len) {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return 0;
    }

    if (len > 0) {
        if (write_pos_ <= buffer_end_) {
            size_t size = buffer_end_ - write_pos_;
            if (static_cast<int32_t>(size) <= len) {
                write_pos_ += size;
                return static_cast<uint32_t>(size);
            } else {
                write_pos_ += len;
                return static_cast<uint32_t>(len);
            }
        } else {
            LOG_ERROR("write_pos_ <= buffer_end_ is false");
            return 0;
        }

    } else {
        len = -len;
        if (read_pos_ <= write_pos_) {
            size_t size = write_pos_ - read_pos_;
            if (static_cast<int32_t>(size) <= len) {
                Clear();
                return static_cast<uint32_t>(size);
            } else {
                write_pos_ -= len;
                return static_cast<uint32_t>(len);
            }
        } else {
            LOG_ERROR("read_pos_ <= write_pos_ is false");
            return 0;
        }
    }
}

// Replace the backing chunk and refresh internal pointers.
void SingleBlockBuffer::Reset(std::shared_ptr<IBufferChunk> chunk) {
    chunk_ = std::move(chunk);
    InitializePointers();
}

std::shared_ptr<IBufferChunk> SingleBlockBuffer::GetChunk() const {
    return chunk_;
}

std::shared_ptr<IBuffer> SingleBlockBuffer::ShallowClone() const {
    auto clone = std::make_shared<SingleBlockBuffer>();
    clone->chunk_ = chunk_;
    clone->buffer_start_ = buffer_start_;
    clone->buffer_end_ = buffer_end_;
    clone->read_pos_ = read_pos_;
    clone->write_pos_ = write_pos_;
    return clone;
}

// Shared implementation used by Read and ReadNotMovePt.
uint32_t SingleBlockBuffer::InnerRead(uint8_t* data, uint32_t len, bool move_pt) {
    if (!Valid()) {
        return 0;
    }

    if (read_pos_ <= write_pos_) {
        size_t size = write_pos_ - read_pos_;
        if (size <= len) {
            std::memcpy(data, read_pos_, size);
            if (move_pt) {
                Clear();
            }
            return static_cast<uint32_t>(size);
        } else {
            std::memcpy(data, read_pos_, len);
            if (move_pt) {
                read_pos_ += len;
            }
            return len;
        }
    } else {
        LOG_ERROR("read_pos_ <= write_pos_ is false");
        return 0;
    }
}

// Internal helper used by Write to perform the actual copy.
uint32_t SingleBlockBuffer::InnerWrite(const uint8_t* data, uint32_t len) {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        return 0;
    }

    if (write_pos_ <= buffer_end_) {
        size_t size = buffer_end_ - write_pos_;
        if (size <= len) {
            std::memcpy(write_pos_, data, size);
            write_pos_ += size;
            return static_cast<uint32_t>(size);
        } else {
            std::memcpy(write_pos_, data, len);
            write_pos_ += len;
            return len;
        }
    } else {
        LOG_ERROR("write_pos_ <= buffer_end_ is false");
        return 0;
    }
}

// Initialize or reset pointer state derived from the current chunk_.
void SingleBlockBuffer::InitializePointers() {
    if (!Valid()) {
        read_pos_ = nullptr;
        write_pos_ = nullptr;
        buffer_start_ = nullptr;
        buffer_end_ = nullptr;
        return;
    }

    buffer_start_ = chunk_->GetData();
    buffer_end_ = buffer_start_ + chunk_->GetLength();
    read_pos_ = buffer_start_;
    write_pos_ = buffer_start_;
}

}
}

