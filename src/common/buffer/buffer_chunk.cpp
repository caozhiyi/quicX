#include <cstdlib>

#include "common/log/log.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chunk.h"

namespace quicx {
namespace common {

BufferChunk::BufferChunk(const std::shared_ptr<BlockMemoryPool>& pool) {
    if (!pool) {
        LOG_ERROR("pool is nullptr");
        return;
    }

    pool_ = pool;
    data_ = static_cast<uint8_t*>(pool->PoolLargeMalloc());
    length_ = pool->GetBlockLength();

    if (data_ == nullptr) {
        length_ = 0;
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
    write_floor_offset_ = other.write_floor_offset_;
    freeze_count_ = other.freeze_count_;

    other.data_ = nullptr;
    other.length_ = 0;
    other.write_floor_offset_ = 0;
    other.freeze_count_ = 0;
}

// Move assignment transfers ownership of the block. Any currently owned block
// is returned to the pool prior to the transfer.
BufferChunk& BufferChunk::operator=(BufferChunk&& other) noexcept {
    if (this != &other) {
        Release();

        pool_ = std::move(other.pool_);
        data_ = other.data_;
        length_ = other.length_;
        write_floor_offset_ = other.write_floor_offset_;
        freeze_count_ = other.freeze_count_;

        other.data_ = nullptr;
        other.length_ = 0;
        other.write_floor_offset_ = 0;
        other.freeze_count_ = 0;
    }
    return *this;
}

std::shared_ptr<BlockMemoryPool> BufferChunk::GetPool() const {
    return pool_.lock();
}

// ----- Zero-copy invariant 1 (write-floor / freeze) ----------------------

// Pin bytes in [data_, end) so subsequent writes cannot touch them. The
// chunk tracks the highest watermark that any live span has installed,
// and a reference count of the outstanding spans. When the last span is
// released the watermark resets, allowing the buffer to be reused fully.
void BufferChunk::FreezeUpTo(uint8_t* end) {
    if (!data_ || end == nullptr) {
        return;
    }
    if (end <= data_) {
        // A span that covers no bytes still increments the ref count so that
        // Unfreeze() pairs cleanly with the matching ctor.
        ++freeze_count_;
        return;
    }
    uint32_t offset = static_cast<uint32_t>(end - data_);
    if (offset > length_) {
        offset = length_;
    }
    if (offset > write_floor_offset_) {
        write_floor_offset_ = offset;
    }
    ++freeze_count_;
}

void BufferChunk::Unfreeze(uint8_t* /*end*/) {
    if (freeze_count_ == 0) {
        return;
    }
    --freeze_count_;
    if (freeze_count_ == 0) {
        write_floor_offset_ = 0;
    }
}

uint8_t* BufferChunk::GetWriteFloor() const {
    if (!data_) {
        return nullptr;
    }
    return data_ + write_floor_offset_;
}

// Return the block to the original pool (if the pool is still alive) and clear
// state. This helper is invoked from both the destructor and move assignment.
//
// IMPORTANT: If the originating pool has already been destroyed (pool_.lock()
// returns nullptr), we MUST free the raw block directly. Otherwise the memory
// leaks, because BlockMemoryPool::Expansion() uses malloc() to acquire blocks
// and only frees blocks that are currently residing in its free list at
// destruction time. Any block that was "checked out" into a BufferChunk when
// the pool died would be lost unless the chunk cleans it up here.
void BufferChunk::Release() {
    if (data_ != nullptr) {
        auto pool = pool_.lock();
        if (pool) {
            // Cast uint8_t* to void* and pass by reference so it can be nullified
            void* ptr = static_cast<void*>(data_);
            pool->PoolLargeFree(ptr);
            // Update data_ after PoolLargeFree (which may modify ptr)
            data_ = static_cast<uint8_t*>(ptr);
        } else {
            // Pool already destroyed - free the raw memory ourselves to avoid leak.
            // BlockMemoryPool::Expansion uses malloc(), so free() is the correct
            // deallocator here.
            free(data_);
        }

        // Ensure data_ is cleared even if pool is gone
        data_ = nullptr;
        length_ = 0;
        write_floor_offset_ = 0;
        freeze_count_ = 0;
    }

    pool_.reset();
}

}
}

