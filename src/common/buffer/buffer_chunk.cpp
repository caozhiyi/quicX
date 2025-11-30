#include "common/log/log.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"

namespace quicx {
namespace common {

// Attempt to allocate a single block from the provided pool and take ownership
// of it. This function never throws. Any failure is logged and represented by
// an invalid chunk (data_ == nullptr, length_ == 0).
BufferChunk::BufferChunk(const std::shared_ptr<BlockMemoryPool>& pool) {
    if (!pool) {
        LOG_ERROR("pool is nullptr");
        return;
    }

    pool_ = pool;
    data_ = static_cast<uint8_t*>(pool->PoolLargeMalloc());
    length_ = pool->GetBlockLength();
    limit_size_ = length_;  // Initialize limit_size_ to full length

    if (data_ == nullptr) {
        length_ = 0;
        limit_size_ = 0;
        LOG_ERROR("failed to allocate memory");
    }
}

BufferChunk::~BufferChunk() {
    Release();
}

// Move constructor transfers ownership of the block, leaving the source in an
// empty state. The underlying memory is not reallocated.
BufferChunk::BufferChunk(BufferChunk&& other) noexcept {
    pool_ = std::move(other.pool_);
    data_ = other.data_;
    length_ = other.length_;
    limit_size_ = other.limit_size_;

    other.data_ = nullptr;
    other.length_ = 0;
    other.limit_size_ = 0;
}

// Move assignment transfers ownership of the block. Any currently owned block
// is returned to the pool prior to the transfer.
BufferChunk& BufferChunk::operator=(BufferChunk&& other) noexcept {
    if (this != &other) {
        Release();

        pool_ = std::move(other.pool_);
        data_ = other.data_;
        length_ = other.length_;
        limit_size_ = other.limit_size_;

        other.data_ = nullptr;
        other.length_ = 0;
        other.limit_size_ = 0;
    }
    return *this;
}

std::shared_ptr<BlockMemoryPool> BufferChunk::GetPool() const {
    return pool_.lock();
}

// Return the block to the original pool (if the pool is still alive) and clear
// state. This helper is invoked from both the destructor and move assignment.
void BufferChunk::Release() {
    if (data_ != nullptr) {
        auto pool = pool_.lock();
        if (pool) {
            // Cast uint8_t* to void* and pass by reference so it can be nullified
            void* ptr = static_cast<void*>(data_);
            pool->PoolLargeFree(ptr);
            // Update data_ after PoolLargeFree (which may modify ptr)
            data_ = static_cast<uint8_t*>(ptr);
        }

        // Ensure data_ is cleared even if pool is gone
        data_ = nullptr;
        length_ = 0;
        limit_size_ = 0;
    }

    pool_.reset();
}

}
}

