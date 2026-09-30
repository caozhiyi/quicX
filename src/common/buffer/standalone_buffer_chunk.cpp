#include "common/buffer/standalone_buffer_chunk.h"

#include <vector>

namespace quicx {
namespace common {

namespace {

// Per-thread recycle depth. Bounded so a transient burst of in-flight
// coalesced packets cannot pin unbounded memory; the excess is freed.
// Typical steady state keeps 1-2 chunks per worker thread.
constexpr size_t kMaxPooledChunks = 512;

thread_local std::vector<StandaloneBufferChunk*> t_chunk_pool;

void RecycleChunk(StandaloneBufferChunk* chunk) {
    if (chunk != nullptr && t_chunk_pool.size() < kMaxPooledChunks) {
        t_chunk_pool.push_back(chunk);
    } else {
        delete chunk;
    }
}

}  // namespace

StandaloneBufferChunk::StandaloneBufferChunk(uint32_t size) {
    if (size == 0) {
        return;
    }
    data_ = new uint8_t[size];
    length_ = size;
    capacity_ = size;
}

StandaloneBufferChunk::~StandaloneBufferChunk() {
    Release();
}

StandaloneBufferChunk::StandaloneBufferChunk(StandaloneBufferChunk&& other) noexcept {
    data_ = other.data_;
    length_ = other.length_;
    capacity_ = other.capacity_;
    write_floor_offset_ = other.write_floor_offset_;
    freeze_count_ = other.freeze_count_;
    other.data_ = nullptr;
    other.length_ = 0;
    other.capacity_ = 0;
    other.write_floor_offset_ = 0;
    other.freeze_count_ = 0;
}

StandaloneBufferChunk& StandaloneBufferChunk::operator=(StandaloneBufferChunk&& other) noexcept {
    if (this != &other) {
        Release();

        data_ = other.data_;
        length_ = other.length_;
        capacity_ = other.capacity_;
        write_floor_offset_ = other.write_floor_offset_;
        freeze_count_ = other.freeze_count_;
        other.data_ = nullptr;
        other.length_ = 0;
        other.capacity_ = 0;
        other.write_floor_offset_ = 0;
        other.freeze_count_ = 0;
    }
    return *this;
}

void StandaloneBufferChunk::Release() {
    delete[] data_;
    data_ = nullptr;
    length_ = 0;
    capacity_ = 0;
    write_floor_offset_ = 0;
    freeze_count_ = 0;
}

void StandaloneBufferChunk::Reuse(uint32_t size) {
    length_ = size;
    // Defensive: freeze state should already be balanced back to zero by the
    // paired FreezeUpTo/Unfreeze calls of the previous lifetime.
    write_floor_offset_ = 0;
    freeze_count_ = 0;
}

std::shared_ptr<StandaloneBufferChunk> StandaloneBufferChunk::Acquire(uint32_t size) {
    if (size == 0) {
        return nullptr;
    }
    // LIFO scan: the most recently recycled chunk is cache-hot and, with
    // stable send sizes, its capacity already covers the request.
    for (size_t i = t_chunk_pool.size(); i > 0; --i) {
        StandaloneBufferChunk* chunk = t_chunk_pool[i - 1];
        if (chunk->capacity_ >= size) {
            t_chunk_pool.erase(t_chunk_pool.begin() + (i - 1));
            chunk->Reuse(size);
            return std::shared_ptr<StandaloneBufferChunk>(chunk, RecycleChunk);
        }
    }
    auto* chunk = new StandaloneBufferChunk(size);
    return std::shared_ptr<StandaloneBufferChunk>(chunk, RecycleChunk);
}

// ----- Zero-copy invariant 1 (write-floor / freeze) ----------------------

void StandaloneBufferChunk::FreezeUpTo(uint8_t* end) {
    if (!data_ || end == nullptr) {
        return;
    }
    if (end <= data_) {
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

void StandaloneBufferChunk::Unfreeze(uint8_t* /*end*/) {
    if (freeze_count_ == 0) {
        return;
    }
    --freeze_count_;
    if (freeze_count_ == 0) {
        write_floor_offset_ = 0;
    }
}

uint8_t* StandaloneBufferChunk::GetWriteFloor() const {
    if (!data_) {
        return nullptr;
    }
    return data_ + write_floor_offset_;
}

}  // namespace common
}  // namespace quicx
