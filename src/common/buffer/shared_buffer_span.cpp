#include "common/buffer/shared_buffer_span.h"

namespace quicx {
namespace common {

// Construct a span that keeps the chunk alive. The constructor validates the
// provided address range and clears the instance on failure.
SharedBufferSpan::SharedBufferSpan(std::shared_ptr<IBufferChunk> chunk, uint8_t* start, uint8_t* end):
    chunk_(std::move(chunk)),
    start_(start),
    end_(end) {

    if (!Valid()) {
        chunk_.reset();
        start_ = nullptr;
        end_ = nullptr;
        return;
    }

    // Pin the bytes [chunk_start, end_) so the owning buffer cannot reuse
    // them while this span is alive. The matching Unfreeze happens in the
    // destructor / copy-assignment / move-assignment.
    if (chunk_) {
        chunk_->FreezeUpTo(end_);
    }
}

SharedBufferSpan::SharedBufferSpan(std::shared_ptr<IBufferChunk> chunk, uint8_t* start, uint32_t len):
    SharedBufferSpan(std::move(chunk), start, start ? start + len : nullptr) {}

SharedBufferSpan::~SharedBufferSpan() {
    if (chunk_) {
        chunk_->Unfreeze(end_);
    }
}

// Copy: install an independent freeze on the chunk so that destructing this
// copy releases its own freeze and never affects the source.
SharedBufferSpan::SharedBufferSpan(const SharedBufferSpan& other):
    chunk_(other.chunk_),
    start_(other.start_),
    end_(other.end_) {
    if (chunk_) {
        chunk_->FreezeUpTo(end_);
    }
}

SharedBufferSpan& SharedBufferSpan::operator=(const SharedBufferSpan& other) {
    if (this == &other) {
        return *this;
    }
    // Release current freeze first so the chunk's freeze_count_ is accurate
    // even if the new span happens to point into the same chunk.
    if (chunk_) {
        chunk_->Unfreeze(end_);
    }
    chunk_ = other.chunk_;
    start_ = other.start_;
    end_ = other.end_;
    if (chunk_) {
        chunk_->FreezeUpTo(end_);
    }
    return *this;
}

// Move: transfer the existing freeze without touching the count. The source
// is reset to a state that performs no Unfreeze on destruction.
SharedBufferSpan::SharedBufferSpan(SharedBufferSpan&& other) noexcept:
    chunk_(std::move(other.chunk_)),
    start_(other.start_),
    end_(other.end_) {
    other.start_ = nullptr;
    other.end_ = nullptr;
}

SharedBufferSpan& SharedBufferSpan::operator=(SharedBufferSpan&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (chunk_) {
        chunk_->Unfreeze(end_);
    }
    chunk_ = std::move(other.chunk_);
    start_ = other.start_;
    end_ = other.end_;
    other.start_ = nullptr;
    other.end_ = nullptr;
    return *this;
}

bool SharedBufferSpan::Valid() const {
    if (!chunk_ || !chunk_->Valid() || start_ == nullptr || end_ == nullptr || start_ > end_) {
        return false;
    }

    uint8_t* chunk_start = chunk_->GetData();
    uint8_t* chunk_end = chunk_start + chunk_->GetLength();
    return start_ >= chunk_start && end_ <= chunk_end;
}

uint8_t* SharedBufferSpan::GetStart() const {
    return Valid() ? start_ : nullptr;
}

uint8_t* SharedBufferSpan::GetEnd() const {
    return Valid() ? end_ : nullptr;
}

uint32_t SharedBufferSpan::GetLength() const {
    return Valid() ? static_cast<uint32_t>(end_ - start_) : 0;
}

BufferSpan SharedBufferSpan::GetSpan() const {
    return BufferSpan(start_, end_);
}
}
}
