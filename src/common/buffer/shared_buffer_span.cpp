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
    }
}

SharedBufferSpan::SharedBufferSpan(std::shared_ptr<IBufferChunk> chunk, uint8_t* start, uint32_t len):
    SharedBufferSpan(std::move(chunk), start, start ? start + len : nullptr) {}

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

