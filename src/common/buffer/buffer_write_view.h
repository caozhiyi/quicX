#ifndef COMMON_BUFFER_BUFFER_WRITE_VIEW
#define COMMON_BUFFER_BUFFER_WRITE_VIEW

#include <cstdint>
#include "common/buffer/buffer_span.h"

namespace quicx {
namespace common {

// BufferWriteView mirrors BufferReadView but focuses on write-only access to an
// externally managed memory range. It never owns the backing storage: callers
// must ensure the provided span stays valid for the lifetime of the view. The
// class offers a minimal API to sequentially append data or skip bytes while
// staying inside the declared range.
class BufferWriteView {
public:
    BufferWriteView();
    BufferWriteView(uint8_t* start, uint32_t len);
    BufferWriteView(uint8_t* start, uint8_t* end);

    // Re-point the view at a new writable range. Invalid inputs reset the view
    // to an empty state and are logged for easier debugging.
    void Reset(uint8_t* start, uint32_t len);
    void Reset(uint8_t* start, uint8_t* end);

    // Copy |len| bytes from |data| into the writable region and advance the
    // internal write pointer. The actual copied length is capped by the
    // remaining capacity. Returns the number of bytes written.
    uint32_t Write(const uint8_t* data, uint32_t len);

    // Move the write pointer by |len| bytes without touching memory. Positive
    // values advance the pointer, negative values rewind it. The movement is
    // always clamped to the writable range and the actual distance moved is
    // returned to the caller.
    uint32_t MoveWritePt(int32_t len);

    // Remaining writable bytes.
    uint32_t GetFreeLength() const;
    // Bytes already produced since the last Reset().
    uint32_t GetDataLength() const;
    // Pointer to the next writable location (or nullptr when invalid).
    uint8_t* GetData() const;

    // Non-owning span describing the still-writable area.
    BufferSpan GetWritableSpan() const;
    // Non-owning span describing the bytes already written.
    BufferSpan GetWrittenSpan() const;

    bool Valid() const;

private:
    uint8_t* buffer_start_ = nullptr;
    uint8_t* buffer_end_ = nullptr;
    uint8_t* write_pos_ = nullptr;
};

}
}

#endif

