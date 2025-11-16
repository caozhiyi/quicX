#include "common/log/log.h"
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace common {

// Construct a view over [start, end). Invalid inputs are logged and the span is
// reset to an empty range to avoid leaking dangling pointers to callers.
BufferSpan::BufferSpan(uint8_t* start, uint8_t* end):
    start_(start),
    end_(end) {
    if (!Valid()) {
        LOG_ERROR("buffer is invalid");
        start_ = nullptr;
        end_ = nullptr;
    }
}

BufferSpan::BufferSpan(uint8_t* start, uint32_t len):
    BufferSpan(start, start ? start + len : nullptr) {}

bool BufferSpan::Valid() const {
    if (!start_ || !end_ || start_ > end_) {
        LOG_ERROR("buffer is invalid");
        return false;
    }
    
    return start_ <= end_;
}

uint8_t* BufferSpan::GetStart() const {
    return start_;
}

uint8_t* BufferSpan::GetEnd() const {
    return end_;
}

uint32_t BufferSpan::GetLength() const {
    return static_cast<uint32_t>(end_ - start_);
}

}
}

