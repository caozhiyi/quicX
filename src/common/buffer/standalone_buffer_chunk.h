#ifndef COMMON_BUFFER_STANDALONE_BUFFER_CHUNK
#define COMMON_BUFFER_STANDALONE_BUFFER_CHUNK

#include <cstdint>
#include <memory>

#include "common/buffer/if_buffer_chunk.h"

namespace quicx {
namespace common {

/**
 * @brief A buffer chunk that manages memory independently (not from a pool)
 *
 * This class uses standard `new` and `delete` for memory management.
 * It is useful for test scenarios or when a buffer with a specific size
 * that doesn't fit into the standard block pool is required.
 */
class StandaloneBufferChunk: public IBufferChunk {
public:
    explicit StandaloneBufferChunk(uint32_t size);
    ~StandaloneBufferChunk();

    StandaloneBufferChunk(const StandaloneBufferChunk&) = delete;
    StandaloneBufferChunk& operator=(const StandaloneBufferChunk&) = delete;

    StandaloneBufferChunk(StandaloneBufferChunk&& other) noexcept;
    StandaloneBufferChunk& operator=(StandaloneBufferChunk&& other) noexcept;

    bool Valid() const override { return data_ != nullptr; }
    uint8_t* GetData() const override { return data_; }
    // Physical block size; immutable for the chunk's lifetime (invariant 2).
    uint32_t GetLength() const override { return length_; }
    std::shared_ptr<BlockMemoryPool> GetPool() const override { return nullptr; }

    // ----- Zero-copy invariant 1 (write-floor / freeze) --------------------
    void FreezeUpTo(uint8_t* end) override;
    void Unfreeze(uint8_t* end) override;
    uint8_t* GetWriteFloor() const override;

private:
    void Release();

    uint8_t* data_ = nullptr;
    uint32_t length_ = 0;

    // See BufferChunk for the semantics of write_floor_offset_/freeze_count_.
    uint32_t write_floor_offset_ = 0;
    uint32_t freeze_count_ = 0;
};

}  // namespace common
}  // namespace quicx

#endif
