#ifndef COMMON_BUFFER_BUFFER_READER
#define COMMON_BUFFER_BUFFER_READER

#include <cstdint>
#include <functional>
#include <memory>

#include "common/buffer/buffer_span.h"
#include "common/include/if_buffer_read.h"

namespace quicx {
namespace common {

class IBuffer;

// BufferReader provides read-only operations with an independent read cursor.
// It supports two modes:
//   - Contiguous mode: operates directly on a (start, end) pointer range.
//     Zero-copy, no heap allocation. Caller must ensure the memory outlives the reader.
//   - IBuffer mode: reads through an IBuffer interface with an independent offset.
//     Supports multi-block buffers. Does not modify the underlying buffer's
//     read pointer until Sync() is called.
class BufferReader : public virtual IBufferRead {
public:
    BufferReader();

    // Contiguous memory mode (replaces BufferReadView)
    BufferReader(uint8_t* start, uint8_t* end);
    BufferReader(uint8_t* start, uint32_t len);

    // IBuffer mode (replaces MultiBlockBufferReadView)
    explicit BufferReader(std::shared_ptr<IBuffer> buffer);

    // Reset to contiguous memory
    void Reset(uint8_t* start, uint8_t* end);
    void Reset(uint8_t* start, uint32_t len);

    // Reset to IBuffer mode
    void Reset(std::shared_ptr<IBuffer> buffer);

    // IBufferRead interface
    uint32_t ReadNotMovePt(uint8_t* data, uint32_t len) override;
    uint32_t MoveReadPt(uint32_t len) override;
    uint32_t Read(uint8_t* data, uint32_t len) override;
    void VisitData(const std::function<bool(uint8_t*, uint32_t)>& visitor) override;
    uint32_t GetDataLength() override;
    uint32_t GetDataLength() const;
    void Clear() override;

    // IBuffer mode: sync offset back to the underlying buffer's read pointer
    void Sync();

    // IBuffer mode: bytes consumed but not yet synced
    uint32_t GetReadOffset() const;

    // Contiguous mode: raw pointer to current read position
    uint8_t* GetData() const;

    // Contiguous mode: span from current read position to end
    BufferSpan GetReadableSpan() const;

    bool Valid() const;
    bool IsContiguous() const { return is_contiguous_; }

private:
    // Shared read implementation for contiguous mode
    uint32_t ContiguousRead(uint8_t* data, uint32_t len, bool move_pt);

    // Contiguous mode state
    uint8_t* read_pos_ = nullptr;
    uint8_t* buffer_start_ = nullptr;
    uint8_t* buffer_end_ = nullptr;

    // IBuffer mode state
    std::shared_ptr<IBuffer> buffer_;
    uint32_t read_offset_ = 0;

    bool is_contiguous_ = true;
};

}  // namespace common
}  // namespace quicx

#endif
