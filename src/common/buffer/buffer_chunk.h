#ifndef COMMON_BUFFER_BUFFER_CHUNK
#define COMMON_BUFFER_BUFFER_CHUNK

#include <cstdint>
#include <memory>

#include "common/buffer/if_buffer_chunk.h"

namespace quicx {
namespace common {

class BlockMemoryPool;

// BufferChunk is the lowest-level building block in the buffer module. It
// owns exactly one fixed-size memory block sourced from a BlockMemoryPool and
// guarantees RAII semantics: once the BufferChunk instance leaves scope the
// memory is automatically returned to the originating pool. Higher level buffer
// abstractions (SingleBlockBuffer, MultiBlockBuffer, SharedBufferSpan, …) never
// talk to BlockMemoryPool directly; they operate on BufferChunk or one of its
// lightweight views instead.
class BufferChunk:
    public IBufferChunk {
public:
    // Acquire a block from the provided pool. The constructor never throws; in
    // case of failure the returned BufferChunk is simply marked invalid. A
    // nullptr pool yields an invalid chunk and logs an error.
    BufferChunk(const std::shared_ptr<BlockMemoryPool>& pool);
    ~BufferChunk();

    BufferChunk(const BufferChunk&) = delete;
    BufferChunk& operator=(const BufferChunk&) = delete;

    BufferChunk(BufferChunk&& other) noexcept;
    BufferChunk& operator=(BufferChunk&& other) noexcept;

    // Returns true when the chunk successfully owns pool-managed memory. All
    // other getters should only be used while Valid() returns true.
    bool Valid() const override { return data_ != nullptr; }
    // Raw pointer to the beginning of the owned block. nullptr if invalid.
    uint8_t* GetData() const override { return data_; }
    // Physical block size; immutable for the chunk's lifetime (invariant 2).
    uint32_t GetLength() const override { return length_; }
    std::shared_ptr<BlockMemoryPool> GetPool() const override;

    // ----- Zero-copy invariant 1 (write-floor / freeze) --------------------
    void FreezeUpTo(uint8_t* end) override;
    void Unfreeze(uint8_t* end) override;
    uint8_t* GetWriteFloor() const override;

private:
    // Return the memory block to the pool (if any) and reset state.
    void Release();

    std::weak_ptr<BlockMemoryPool> pool_;
    uint8_t* data_ = nullptr;
    uint32_t length_ = 0;

    // Highest byte offset that has been frozen by an outstanding
    // SharedBufferSpan. While freeze_count_ > 0, writes that would land below
    // (data_ + write_floor_offset_) MUST be silently clamped or rejected.
    // Once freeze_count_ drops to zero, write_floor_offset_ resets to 0.
    uint32_t write_floor_offset_ = 0;
    uint32_t freeze_count_ = 0;
};

}
}

#endif

