#ifndef COMMON_BUFFERNEW_SHARED_BUFFER_SPAN
#define COMMON_BUFFERNEW_SHARED_BUFFER_SPAN

#include <cstdint>
#include <memory>

#include "common/buffer/buffer_span.h"
#include "common/buffer/if_buffer_chunk.h"

namespace quicx {
namespace common {

// SharedBufferSpan behaves like BufferSpan but retains shared ownership of the
// underlying BufferChunk. It is ideal for handing buffer slices across threads
// or asynchronous boundaries where lifetime guarantees are harder to reason
// about. As long as the span is alive the underlying chunk remains allocated.
class SharedBufferSpan {
public:
    SharedBufferSpan() = default;
    // Construct from a chunk and explicit start/end pointers. The provided
    // pointers must lie within the chunk; otherwise the span is reset to an
    // empty/invalid state and a log entry is emitted.
    SharedBufferSpan(std::shared_ptr<IBufferChunk> chunk, uint8_t* start, uint8_t* end);
    // Convenience overload that accepts a length in bytes.
    SharedBufferSpan(std::shared_ptr<IBufferChunk> chunk, uint8_t* start, uint32_t len);

    // Returns true when the span owns a valid chunk and the range is inside the
    // chunk's boundaries.
    bool Valid() const;

    // Accessors mirror BufferSpan. When the span is invalid a safe default is
    // returned (nullptr/0) to prevent accidental dereferences.
    std::shared_ptr<IBufferChunk> GetChunk() const { return chunk_; }
    uint8_t* GetStart() const;
    uint8_t* GetEnd() const;
    uint32_t GetLength() const;
    BufferSpan GetSpan() const;

private:
    std::shared_ptr<IBufferChunk> chunk_;
    uint8_t* start_ = nullptr;
    uint8_t* end_ = nullptr;
};

}
}

#endif

