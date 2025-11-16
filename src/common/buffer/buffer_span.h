#ifndef COMMON_BUFFERNEW_BUFFER_SPAN
#define COMMON_BUFFERNEW_BUFFER_SPAN

#include <cstdint>

namespace quicx {
namespace common {

class BufferChunk;

// BufferSpan is a minimal, non-owning view over a contiguous memory region.
// Unlike SharedBufferSpan it does not retain the BufferChunk (or any other
// owner) that backs the memory – callers must therefore ensure the underlying
// storage outlives the span. The class performs basic sanity checks on the
// pointer range but assumes the caller maintains lifetime guarantees.
class BufferSpan {
public:
    BufferSpan() = default;
    // Construct a span from raw start/end pointers. The constructor validates
    // the range (start <= end and both non-null) and will reset the span to an
    // empty state if the check fails.
    BufferSpan(uint8_t* start, uint8_t* end);
    // Convenience overload that accepts a start pointer and a length.
    BufferSpan(uint8_t* start, uint32_t len);

    // Returns true when the span currently refers to a valid range. Failed
    // ranges are reported via the logging subsystem for easier debugging.
    bool Valid() const;

    // Raw accessors. Callers should only use them when Valid() returns true.
    uint8_t* GetStart() const;
    uint8_t* GetEnd() const;
    uint32_t GetLength() const;

private:
    uint8_t* start_ = nullptr;
    uint8_t* end_ = nullptr;
};

}
}

#endif

