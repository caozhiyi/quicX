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

    // Pooled acquire for the coalesce-on-demand hot path
    // (MultiBlockBuffer::GetCoalescedReadable). Historically each coalesce
    // did make_shared<StandaloneBufferChunk>(take), i.e. one malloc for the
    // control block + one new[] for the payload — and the resulting chunk
    // stays alive until the packet is ACKed (frames outlive the build via
    // unacked_packets_), so a simple reuse-buffer is unsafe. Instead the
    // returned shared_ptr carries a recycling deleter: when the last frame
    // referencing the chunk is destroyed, the object goes back to a
    // thread-local freelist instead of being freed. Steady state therefore
    // costs zero allocations. Chunks are reused whenever their capacity
    // covers the requested size (send sizes are stable in practice, so the
    // first probe hits). The deleter runs on whichever thread drops the
    // last reference; since packets are built and retired on the same
    // worker thread, recycled chunks never migrate in practice — and even
    // if they did, each pool is only ever touched by its owning thread.
    static std::shared_ptr<StandaloneBufferChunk> Acquire(uint32_t size);

private:
    StandaloneBufferChunk() = default;

    void Release();
    // Re-arm a recycled chunk for a new logical size (<= capacity_).
    void Reuse(uint32_t size);

    uint8_t* data_ = nullptr;
    uint32_t length_ = 0;
    uint32_t capacity_ = 0;  // allocated bytes behind data_; 0 when heap-less

    // See BufferChunk for the semantics of write_floor_offset_/freeze_count_.
    uint32_t write_floor_offset_ = 0;
    uint32_t freeze_count_ = 0;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_BUFFER_STANDALONE_BUFFER_CHUNK
