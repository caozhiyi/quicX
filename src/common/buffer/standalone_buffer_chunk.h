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
    void SetLimitSize(uint32_t size) override { limit_size_ = size; }
    uint32_t GetLength() const override { return std::min(length_, limit_size_); }
    std::shared_ptr<BlockMemoryPool> GetPool() const override { return nullptr; }

private:
    void Release();

    uint8_t* data_ = nullptr;
    uint32_t length_ = 0;
    uint32_t limit_size_ = 0;
};

}  // namespace common
}  // namespace quicx

#endif
